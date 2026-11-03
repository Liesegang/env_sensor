"""Build local, editable mechanical reference parts for the card enclosure.

Run with FreeCAD's Python interpreter. These are envelope models, not vendor CAD.
"""

from pathlib import Path

import FreeCAD as App
import Part


ROOT = Path(__file__).resolve().parent


def add(doc, name, shape, color, note):
    obj = doc.addObject("PartDesign::Feature", name)
    obj.Label = name
    obj.Shape = shape
    obj.addProperty("App::PropertyString", "DimensionSource", "Reference")
    obj.DimensionSource = note
    if obj.ViewObject is not None:
        obj.ViewObject.ShapeColor = color
    Part.export([obj], str(ROOT / f"{name}.step"))
    return obj


def main():
    doc = App.newDocument("CardEnvSensorCaseParts")
    add(
        doc,
        "Waveshare_3p6_Spectra6_bare_panel_envelope",
        Part.makeBox(86.6, 56.0, 0.78),
        (0.86, 0.85, 0.77),
        "Bare EPD glass outline in docs/datasheets/3.6inch_e-Paper_HAT+.pdf; 86.6 x 56 x 0.78 mm",
    )
    add(
        doc,
        "DTP502535_PHR_battery_envelope",
        Part.makeBox(25.0, 36.0, 5.0),
        (0.45, 0.49, 0.57),
        "Sengoku EEHD-4YZL; nominal pouch size 25 x 36 x 5 mm; lead exit is placement-dependent",
    )
    head = Part.makeCylinder(3.0, 0.7, App.Vector(0, 0, 5.0))
    shank = Part.makeCylinder(1.5, 5.0)
    add(
        doc,
        "FLA_0305E_M3x5_low_head_envelope",
        head.fuse(shank),
        (0.67, 0.68, 0.70),
        "Nitto Seiko F-LAMIMATE-R; M3 x 5 mm, head D6 mm, H0.7 +/- 0.15 mm; nominal envelope only",
    )
    for name, filename in (
        ("SW1_SK_12D11VG3_vendor", "SK-12D11VG3.step"),
        ("S1_LeverSwitch_TMHU27_vendor", "LeverSwitch_TMHU27.step"),
        ("SFA40_vendor", "STEP_file_SFA40.stp"),
    ):
        obj = doc.addObject("PartDesign::Feature", name)
        obj.Label = name
        obj.Shape = Part.read(str(ROOT / filename))
        obj.addProperty("App::PropertyString", "SourceFile", "Reference")
        obj.SourceFile = filename
    doc.recompute()
    doc.saveAs(str(ROOT / "CardEnvSensorCaseParts.FCStd"))


if __name__ == "__main__":
    main()
