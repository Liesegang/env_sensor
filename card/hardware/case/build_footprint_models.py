"""Build local STEP models for every populated v2 footprint type.

Run with FreeCAD's bundled Python and its lib directory on PYTHONPATH.
Models use the EAGLE footprint's XY origin and the component-side PCB face
at Z=0. Element positions and rotations are deliberately not applied here.
The model map distinguishes measured/vendor geometry, safe envelopes, and
generic package assumptions; these are enclosure references, not renderings.
"""

from __future__ import annotations

from collections import defaultdict
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET
import zipfile

# Import order matters with the macOS FreeCAD runtime.
import FreeCAD as App
import Part


HERE = Path(__file__).resolve().parent
OUT = HERE / "fusion_libraries"
MODELS = OUT / "models"
SOURCE = HERE.parent / "v2" / "card_env_sensor_1.fbrd"
VENDOR = HERE.parent / "3dmodels"


def box(bounds):
    x0, x1, y0, y1, z0, z1 = bounds
    return Part.makeBox(x1 - x0, y1 - y0, z1 - z0, App.Vector(x0, y0, z0))


def bounds(shape):
    b = shape.BoundBox
    return dict(zip(("x0", "x1", "y0", "y1", "z0", "z1"),
                    (b.XMin, b.XMax, b.YMin, b.YMax, b.ZMin, b.ZMax)))


def fuse(shapes):
    result = shapes[0]
    for shape in shapes[1:]:
        result = result.fuse(shape)
    return result.removeSplitter()


def chip(length, width, height, terminal_length=None):
    """One body and two end terminations, fused as a single valid solid."""
    t = terminal_length or min(length * 0.2, 0.4)
    body = box((-length / 2, length / 2, -width / 2, width / 2, 0.1, height))
    terminals = [box((a, a + t, -width / 2, width / 2, 0, 0.2))
                 for a in (-length / 2, length / 2 - t)]
    return fuse([body, *terminals])


def diode_sod123():
    body = box((-1.345, 1.345, -0.8, 0.8, 0.1, 1.35))
    terminals = [box((a, a + 0.8, -0.45, 0.45, 0, 0.2))
                 for a in (-1.9, 1.1)]
    return fuse([body, *terminals])


def sot23():
    body = box((-1.5, 1.5, -0.7, 0.7, 0.1, 1.3))
    pins = [box((x - 0.2, x + 0.2, -1.45, -0.55, 0, 0.2))
            for x in (-0.95, 0.95)]
    pins.append(box((-0.2, 0.2, 0.55, 1.45, 0, 0.2)))
    return fuse([body, *pins])


def debug_header():
    # Model the optional 1.27mm six-pin single-row header. Its actual fitted
    # height is unknown and must not silently become a case requirement.
    body = box((-4.445, 3.175, -0.762, 0.762, 0, 1.8))
    posts = [box((x - 0.2, x + 0.2, -0.2, 0.2, -2.1, 5.3))
             for x in (-3.81, -2.54, -1.27, 0, 1.27, 2.54)]
    return fuse([body, *posts])


def generic(package):
    footprint_source = f"v2 embedded EAGLE package {package}, layer 51 body outline / pad origin"
    specifications = {
        "1005A": (1.0, 0.5, 0.65),
        "1005K": (1.0, 0.5, 0.65),
        "1608A": (1.6, 0.8, 1.0),
        "2012A": (2.0, 1.25, 0.7),
        "2015": (2.0, 1.5, 1.3),
        "LED1608": (1.6, 0.8, 0.9),
    }
    if package in specifications:
        l, w, h = specifications[package]
        return chip(l, w, h), {
            "confidence": "low",
            "source": [footprint_source],
            "notes": f"Generic {l}x{w}mm package body; assumed assembled height {h}mm. Exact part/height is unspecified. Shared R/C/inductor footprint uses a single safe nominal shape.",
            "height_assumed": True,
            "model_kind": "generic_package_with_terminals",
        }
    if package in {"SOD-123-2", "1N4148W"}:
        return diode_sod123(), {
            "confidence": "medium",
            "source": [footprint_source, "https://www.nxp.com/packages/SOD123.html"],
            "notes": "SOD123 body from 2.69x1.6mm footprint outline, height assumed conservatively 1.35mm; terminal span 3.8mm. Generic geometry for MBR0530 / 1N4148W; exact vendor tolerance is not identified.",
            "height_assumed": True,
            "model_kind": "generic_package_with_terminals",
        }
    if package == "SOT23-3":
        return sot23(), {
            "confidence": "medium",
            "source": [footprint_source],
            "notes": "Generic SOT23-3 body 3.0x1.4mm, terminal span2.9mm, assumed assembled height1.3mm. Pad positions are x±0.95,y=-1.1 and x0,y=+1.08. Covers IRLML6344/6402 and 2SC3265 footprint types.",
            "height_assumed": True,
            "model_kind": "generic_package_with_terminals",
        }
    if package == "INDC2016X100N":
        return chip(2.2, 1.8, 1.1), {
            "confidence": "medium",
            "source": [footprint_source, "v2 PARTS DFE201610E-2R2M=P2"],
            "notes": "2.0x1.6x1.0mm nominal inductor class; body model uses footprint maximum outline2.2x1.8mm and height1.1mm including solder. No detailed winding/core model.",
            "model_kind": "package_envelope_with_terminals",
        }
    if package == "CST2C":
        shape = chip(1.6, 0.8, 0.58)
        shape.rotate(App.Vector(), App.Vector(0, 0, 1), 90)
        return shape, {
            "confidence": "medium",
            "source": [footprint_source, "https://toshiba.semicon-storage.com/us/semiconductor/design-development/package/diode.html"],
            "notes": "Toshiba CST2C nominal1.6x0.8x0.48mm, plus0.1mm solder. Long axis along local Y per embedded footprint; not per-board rotation.",
            "model_kind": "generic_package_with_terminals",
        }
    if package == "1X6IDC_1R27":
        return debug_header(), {
            "confidence": "low",
            "source": [footprint_source],
            "notes": "Optional debug header model: six0.4mm square pins on1.27mm pitch, plastic7.62x1.524x1.8mm, assumed top5.3mm, trimmed tails toPCB underside+0.5mm. Actual population and actual height need confirmation.",
            "height_assumed": True,
            "population_pending": True,
            "model_kind": "optional_header_assumption",
        }
    raise KeyError(f"No model definition for {package}")


def vendor_shape(package):
    if package == "SK-12D11VG3":
        shape = Part.read(str(VENDOR / "SK-12D11VG3.step"))
        # The user specified trimmed through-hole leads, 0.5mm below1.6mm PCB.
        shape = shape.cut(box((-30, 30, -30, 30, -20, -2.1))).removeSplitter()
        return shape, {
            "confidence": "medium",
            "source": ["card/hardware/3dmodels/SK-12D11VG3.step", "card/hardware/3dmodels/SK-12D11VG3.SOURCE.md"],
            "notes": "Existing supplier STEP at its original XY origin; through-hole legs clipped atz=-2.1mm for actual1.6mm PCB and0.5mm remaining tails. Unchanged assembled top4.8mm.",
            "model_kind": "vendor_step_trimmed_leads",
        }
    if package == "SW_TMHU27":
        shape = Part.read(str(VENDOR / "LeverSwitch_TMHU27.step"))
        # Vendor model is upside down: seating contacts atz=2.6 and alignment
        # posts atx±2.5,y=0.1,z=2.5..3.05. Flip to +Z-mounted orientation.
        shape.rotate(App.Vector(), App.Vector(1, 0, 0), 180)
        shape.translate(App.Vector(0, -0.1, 2.7))
        return shape, {
            "confidence": "high",
            "source": ["card/hardware/3dmodels/LeverSwitch_TMHU27.step", "docs/datasheets/TMHU27.pdf p.2", "v2 SW_TMHU27 footprint"],
            "notes": "Vendor origin corrected by RX180 then translate(0,-0.1,2.7)mm. Contacts seat atz0.1 (0.1mm solder allowance), postsx±2.5,y=-0.2,z=-0.35..0.2; three contact centresy=3.575 agree with footprint3.55 within0.025mm. Body2.5mm plus0.1mm contact step and0.1mm solder. Footprint element rotation remains unapplied.",
            "source_transform": {"rotation_axis": [1, 0, 0], "rotation_deg": 180, "translation_mm": [0, -0.1, 2.7]},
            "alignment_confirmed": True,
            "model_kind": "vendor_step_origin_corrected",
        }
    raise KeyError(package)


def main():
    with zipfile.ZipFile(SOURCE) as archive:
        board_name = next(n for n in archive.namelist() if n.endswith(".brd"))
        board = ET.fromstring(archive.read(board_name)).find("./drawing/board")
    positions = defaultdict(list)
    for element in board.find("elements"):
        key = (element.get("library"), element.get("library_urn"), element.get("package"))
        positions[key].append(element.get("name"))
    major = json.loads((HERE / "component_envelopes.json").read_text())["components"]
    major_by_package = {c["package"]: c for c in major}
    MODELS.mkdir(parents=True, exist_ok=True)
    models = []
    doc = App.newDocument("CardFootprintModels")
    exported = {}
    excluded = {"SFA40", "SFA40-PIN", "SFA40-PIN2", "PT3-POW"}
    for library in board.find("libraries"):
        library_name, urn = library.get("name"), library.get("urn")
        for package in library.findall("./packages/package"):
            name = package.get("name")
            entry = {
                "key": f"{urn or 'noURN'}|{name}",
                "library": library_name,
                "origin_library_urn": urn,
                "package": name,
                "refs": positions[(library_name, urn, name)],
            }
            if name in excluded:
                entry.update(status="excluded", filename=None, confidence="high",
                             source=["card/hardware/readme.md", "v2 board footprint"],
                             notes="No physical component: SFA40 not fitted / off-board drawing, or bare plated power/test point.")
                models.append(entry)
                continue
            try:
                if name in {"SK-12D11VG3", "SW_TMHU27"}:
                    shape, detail = vendor_shape(name)
                elif name in major_by_package:
                    c = major_by_package[name]
                    shape = box([c["bbox"][k] for k in ("x0", "x1", "y0", "y1", "z0", "z1")])
                    detail = {k: c[k] for k in ("confidence", "source", "notes")}
                    detail["model_kind"] = "conservative_body_envelope"
                else:
                    shape, detail = generic(name)
                if not shape.isValid() or not shape.Solids or any(not s.isValid() for s in shape.Solids):
                    raise ValueError("Invalid or non-solid model")
                slug = re.sub(r"[^A-Za-z0-9_-]+", "_", f"{library_name}_{name}").strip("_")
                filename = f"models/{slug}.step"
                if filename not in exported:
                    obj = doc.addObject("PartDesign::Feature", slug)
                    obj.Label = f"{library_name} / {name}"
                    obj.Shape = shape
                    Part.export([obj], str(OUT / filename))
                    restored = Part.read(str(OUT / filename))
                    if not restored.isValid() or not restored.Solids or any(not s.isValid() for s in restored.Solids):
                        raise ValueError("STEP round-trip geometry invalid")
                    exported[filename] = True
                entry.update(detail)
                entry.update(status="ready", filename=filename, bbox=bounds(shape),
                             all_solids_valid=True, solid_count=len(shape.Solids))
            except Exception as exc:
                entry.update(status="pending", filename=None, confidence="unknown",
                             source=[f"v2 embedded footprint {name}"], notes=str(exc))
            models.append(entry)
    result = {
        "schema_version": 1,
        "units": "mm",
        "coordinate_system": "EAGLE package XY origin before element rotation; PCB component surface z=0. Generic models and envelope models include assembled solder allowance. Vendor transforms are explicitly recorded.",
        "source_board": "card/hardware/v2/card_env_sensor_1.fbrd",
        "status_semantics": "ready means a local STEP is prepared. It does not mean the STEP has been attached to a Fusion 3D package or registered in a Fusion library.",
        "models": models,
        "summary": {
            "library_package_keys": len(models),
            "unique_package_names": len({m["package"] for m in models}),
            "ready": sum(m["status"] == "ready" for m in models),
            "excluded": sum(m["status"] == "excluded" for m in models),
            "pending": sum(m["status"] == "pending" for m in models),
            "unique_step_files": len(exported),
        },
    }
    (OUT / "model_map.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
    App.closeDocument(doc.Name)
    print(json.dumps(result["summary"]))


if __name__ == "__main__":
    main()
