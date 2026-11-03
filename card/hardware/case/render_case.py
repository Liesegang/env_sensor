"""Render the actual FreeCAD case and assembly solids without a GUI.

Run with FreeCAD's bundled Python (numpy, Pillow and matplotlib are bundled):
  PYTHONPATH=/Applications/FreeCAD.app/Contents/Resources/lib \\
    /Applications/FreeCAD.app/Contents/Resources/bin/python render_case.py

Outputs a four-view case_preview.png, a large case_exploded.png, and large
front/rear/internal views. A per-pixel depth buffer hides occluded facets; no
illustrative enclosure geometry is invented. The internal view removes the
rear roof only for inspection, and the exploded view moves the actual parts.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass, replace
from pathlib import Path

import FreeCAD as App  # Must precede Part when using the bundled interpreter.
import Part
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from matplotlib import get_data_path


HERE = Path(__file__).resolve().parent
GENERATED = HERE / "generated"
BACKGROUND = (247, 248, 250)
INK = (37, 46, 56)
MUTED = (100, 113, 126)
FONT_ROOT = Path(get_data_path()) / "fonts" / "ttf"


@dataclass
class RenderPart:
    name: str
    label: str
    role: str
    shape: object
    triangles: np.ndarray
    normals: np.ndarray
    color: tuple[float, float, float]


def font(size: int, bold: bool = False):
    name = "DejaVuSans-Bold.ttf" if bold else "DejaVuSans.ttf"
    return ImageFont.truetype(str(FONT_ROOT / name), size)


def role_for(name: str, label: str) -> str:
    text = f"{name} {label}".lower()
    if name.lower() in {"front", "frontcase", "casefront"} or "front case" in text:
        return "front"
    if name.lower() in {"rear", "back", "rearcase", "caserear", "backcase"} or "rear case" in text:
        return "rear"
    if name.upper() in {"SW1", "S1"}:
        # Switch descriptions mention clipped leads; those are not wires.
        return "component"
    if name.lower().startswith("nut") or "m3 nut" in text:
        return "nut"
    if any(token in text for token in ("screw", "fla_", "fl a-")):
        return "screw"
    if any(token in text for token in ("wire", "lead", "cable")):
        return "wire"
    if any(token in text for token in ("fpc", "flex")):
        return "fpc"
    if any(token in text for token in ("tape", "foam", "adhesive", "fixingpad", "retentionpad")):
        return "pad"
    if any(token in text for token in ("battery", "dtp502535", " cell")):
        return "battery"
    if any(token in text for token in ("epd", "epaper", "e-paper", "display", "panel")):
        return "panel"
    if "pcb" in text or name.lower() == "board":
        return "pcb"
    if any(token in text for token in ("pad", "tape", "foam", "adhesive")):
        return "pad"
    return "component"


def color_for(role: str, name: str) -> tuple[float, float, float]:
    colors = {
        "front": (0.30, 0.37, 0.43),
        "rear": (0.68, 0.74, 0.79),
        "panel": (0.89, 0.88, 0.81),
        "pcb": (0.10, 0.45, 0.31),
        "battery": (0.45, 0.52, 0.60),
        "screw": (0.72, 0.75, 0.77),
        "nut": (0.74, 0.77, 0.80),
        "fpc": (0.90, 0.58, 0.17),
        "wire": (0.76, 0.18, 0.14),
        "pad": (0.29, 0.31, 0.34),
        "component": (0.54, 0.57, 0.60),
    }
    if role == "wire" and any(token in name.lower() for token in ("black", "negative", "gnd")):
        return (0.16, 0.17, 0.19)
    return colors[role]


def prepare_part(name: str, label: str, shape, tolerance: float) -> RenderPart:
    vertices, facets = shape.tessellate(tolerance)
    if not facets:
        raise ValueError(f"No tessellation for {name}")
    points = np.array([(p.x, p.y, p.z) for p in vertices], dtype=np.float64)
    triangles = points[np.asarray(facets, dtype=np.int32)]
    normals = np.cross(triangles[:, 1] - triangles[:, 0], triangles[:, 2] - triangles[:, 0])
    lengths = np.linalg.norm(normals, axis=1)
    keep = lengths > 1e-12
    triangles, normals, lengths = triangles[keep], normals[keep], lengths[keep]
    normals /= lengths[:, None]
    role = role_for(name, label)
    return RenderPart(name, label, role, shape, triangles, normals, color_for(role, name))


def document_parts(path: Path, tolerance: float) -> list[RenderPart]:
    doc = App.openDocument(str(path))
    objects = [obj for obj in doc.Objects if obj.TypeId not in {"App::DocumentObjectGroup", "App::Part"}
               and hasattr(obj, "Shape") and not obj.Shape.isNull()
               and getattr(obj, "FitRole", "Physical") != "Keepout"
               and getattr(obj, "Visibility", True)]
    # A Body's final Shape already includes its features. Avoid drawing both.
    objects = [obj for obj in objects if not any(parent in objects for parent in obj.InList)]
    parts = []
    for obj in objects:
        shape = obj.Shape.copy()
        if not shape.isValid():
            raise ValueError(f"Invalid shape in {path.name}: {obj.Name}")
        parts.append(prepare_part(obj.Name, obj.Label, shape, tolerance))
    if not parts:
        raise ValueError(f"No rendered solids in {path}")
    return parts


def translated(part: RenderPart, distance: float) -> RenderPart:
    triangles = part.triangles.copy()
    triangles[:, :, 2] += distance
    return replace(part, triangles=triangles)


def basis(camera) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    view = np.asarray(camera, dtype=np.float64)
    view /= np.linalg.norm(view)
    right = np.cross(np.array((0.0, 0.0, 1.0)), view)
    if np.linalg.norm(right) < 1e-8:
        right = np.array((1.0, 0.0, 0.0))
    right /= np.linalg.norm(right)
    up = np.cross(view, right)
    return right, up, view


def rasterize(parts: list[RenderPart], width: int, height: int, camera, supersample: int) -> Image.Image:
    """Orthographic triangle rasterization with interpolated per-pixel depth."""
    w, h = width * supersample, height * supersample
    right, up, view = basis(camera)
    matrix = np.stack((right, up, view), axis=1)
    projected = [part.triangles @ matrix for part in parts]
    points = np.concatenate([triangles.reshape(-1, 3) for triangles in projected])
    low, high = points[:, :2].min(axis=0), points[:, :2].max(axis=0)
    center = (low + high) / 2
    span = np.maximum(high - low, 1.0)
    margin = 0.055
    scale = min(w * (1 - 2 * margin) / span[0], h * (1 - 2 * margin) / span[1])
    depth = np.full((h, w), -np.inf, dtype=np.float64)
    pixels = np.empty((h, w, 3), dtype=np.uint8)
    pixels[:] = BACKGROUND
    light = view * 0.82 + up * 0.40 - right * 0.20
    light /= np.linalg.norm(light)

    for part, triangles in zip(parts, projected):
        brightness = 0.40 + 0.45 * np.abs(part.normals @ light) + 0.15 * np.abs(part.normals @ view)
        shades = np.clip(np.asarray(part.color)[None, :] * brightness[:, None] * 255, 0, 255).astype(np.uint8)
        screen = triangles.copy()
        screen[:, :, 0] = (triangles[:, :, 0] - center[0]) * scale + w / 2
        screen[:, :, 1] = -(triangles[:, :, 1] - center[1]) * scale + h / 2
        for tri, shade in zip(screen, shades):
            x0, y0, z0 = tri[0]
            x1, y1, z1 = tri[1]
            x2, y2, z2 = tri[2]
            denominator = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
            if abs(denominator) < 1e-9:
                continue
            xmin, xmax = max(0, int(np.floor(min(x0, x1, x2)))), min(w - 1, int(np.ceil(max(x0, x1, x2))))
            ymin, ymax = max(0, int(np.floor(min(y0, y1, y2)))), min(h - 1, int(np.ceil(max(y0, y1, y2))))
            if xmin > xmax or ymin > ymax:
                continue
            xs = np.arange(xmin, xmax + 1, dtype=np.float64)[None, :] + 0.5
            ys = np.arange(ymin, ymax + 1, dtype=np.float64)[:, None] + 0.5
            a = ((y1 - y2) * (xs - x2) + (x2 - x1) * (ys - y2)) / denominator
            b = ((y2 - y0) * (xs - x2) + (x0 - x2) * (ys - y2)) / denominator
            c = 1 - a - b
            inside = (a >= -1e-9) & (b >= -1e-9) & (c >= -1e-9)
            z = a * z0 + b * z1 + c * z2
            region = depth[ymin:ymax + 1, xmin:xmax + 1]
            visible = inside & (z > region + 1e-9)
            if visible.any():
                region[visible] = z[visible]
                pixels[ymin:ymax + 1, xmin:xmax + 1][visible] = shade
    image = Image.fromarray(pixels, mode="RGB")
    if supersample > 1:
        image = image.resize((width, height), Image.Resampling.LANCZOS)
    return image


def rear_roof_level(rear: RenderPart) -> float:
    """Find the large horizontal inner/outer roof faces, not a local bulge."""
    candidates = []
    for face in rear.shape.Faces:
        bb = face.BoundBox
        if bb.ZLength < 1e-5 and face.Area > 100:
            candidates.append((face.Area, bb.ZMin))
    if len(candidates) >= 2:
        # The main roof's two faces dominate the area; its inside is the lower.
        levels = [z for _, z in sorted(candidates, reverse=True)[:2]]
        return min(levels)
    return rear.shape.BoundBox.ZMax - 0.8


def internal_scene(parts: list[RenderPart], tolerance: float, floor_z: float | None) -> list[RenderPart]:
    result = []
    for part in parts:
        if part.role in {"front", "panel"}:
            continue
        if part.role == "rear":
            z = floor_z if floor_z is not None else rear_roof_level(part)
            bb = part.shape.BoundBox
            tool = Part.makeBox(bb.XLength + 20, bb.YLength + 20, bb.ZLength + 20,
                                App.Vector(bb.XMin - 10, bb.YMin - 10, z - 1e-4))
            section = part.shape.cut(tool)
            if section.isNull():
                raise ValueError("Rear inspection section removed the complete shell")
            cut = prepare_part(part.name, part.label, section, tolerance)
            result.append(replace(cut, color=part.color))
        else:
            result.append(part)
    return result


def exploded_scene(parts: list[RenderPart]) -> list[RenderPart]:
    offsets = {"front": -25.0, "panel": -13.0, "screw": -7.0, "rear": 22.0, "nut": 34.0}
    attached = {"PanelFrontTape": -25.0, "PanelRetentionPads": 22.0, "BatteryFixingPad": 22.0}
    return [translated(part, attached.get(part.name, offsets.get(part.role, 0.0))) for part in parts]


def wrapped_text(draw, text: str, xy, typeface, fill, max_width: int):
    x, y = xy
    line = ""
    for word in text.split():
        next_line = (line + " " + word).strip()
        if line and draw.textlength(next_line, font=typeface) > max_width:
            draw.text((x, y), line, font=typeface, fill=fill)
            y += typeface.size + 5
            line = word
        else:
            line = next_line
    if line:
        draw.text((x, y), line, font=typeface, fill=fill)


def view_image(scene, title: str, subtitle: str, camera, width: int, height: int, supersample: int) -> Image.Image:
    canvas = Image.new("RGB", (width, height), BACKGROUND)
    draw = ImageDraw.Draw(canvas)
    title_size = max(18, round(width * 0.022))
    subtitle_size = max(11, round(width * 0.012))
    top = max(63, round(height * 0.12))
    bottom = max(26, round(height * 0.045))
    draw.text((round(width * 0.035), round(height * 0.028)), title, font=font(title_size, True), fill=INK)
    wrapped_text(draw, subtitle, (round(width * 0.035), round(height * 0.073)), font(subtitle_size), MUTED,
                 round(width * 0.93))
    render = rasterize(scene, width, height - top - bottom, camera, supersample)
    canvas.paste(render, (0, top))
    return canvas


def find_file(explicit: Path | None, names: tuple[str, ...]) -> Path:
    if explicit is not None:
        if not explicit.exists():
            raise FileNotFoundError(explicit)
        return explicit.resolve()
    for name in names:
        path = GENERATED / name
        if path.exists():
            return path
    raise FileNotFoundError(f"None of the expected CAD files exist: {names}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case", type=Path, help="Final case FCStd path")
    parser.add_argument("--stack", type=Path, help="Assembly reference FCStd path")
    parser.add_argument("--output-dir", type=Path, default=GENERATED)
    parser.add_argument("--width", type=int, default=1600)
    parser.add_argument("--height", type=int, default=1000)
    parser.add_argument("--supersample", type=int, default=2)
    parser.add_argument("--tolerance", type=float, default=0.12, help="CAD tessellation tolerance in mm")
    parser.add_argument("--rear-roof-z", type=float, help="Explicit internal roof cut height in assembly coordinates")
    parser.add_argument("--no-singles", action="store_true", help="Save only preview and exploded images")
    args = parser.parse_args()
    if args.width < 400 or args.height < 300 or args.supersample < 1 or args.tolerance <= 0:
        parser.error("Use width >=400, height >=300, supersample >=1 and positive tolerance")
    case_path = find_file(args.case, ("card_env_sensor_case.FCStd", "card_env_sensor_case_draft.FCStd"))
    stack_path = find_file(args.stack, ("card_env_sensor_case_assembly.FCStd", "card_env_sensor_assembly.FCStd",
                                      "card_env_sensor_stack_reference.FCStd"))
    # The complete assembly may already contain the shell halves. Use the
    # explicitly selected case document for those and avoid duplicate solids.
    parts = document_parts(case_path, args.tolerance)
    parts += [part for part in document_parts(stack_path, args.tolerance) if part.role not in {"front", "rear"}]
    shells = [part for part in parts if part.role in {"front", "rear"}]
    if {part.role for part in shells} != {"front", "rear"}:
        raise ValueError("Expected named Front and Rear case solids")
    box = App.BoundBox()
    for part in shells:
        box.add(part.shape.BoundBox)
    dimensions = f"Case CAD: {box.XLength:.2f} x {box.YLength:.2f} x {box.ZLength:.2f} mm"
    front_camera = (0.95, -1.3, -1.5)
    rear_camera = (0.95, -1.3, 1.5)
    views = (
        ("case_front_assembled.png", parts, "Front / assembled", dimensions + "; open display window", front_camera),
        ("case_rear_assembled.png", parts, "Rear / assembled", "Actual sensor slits, switch access and connector relief", rear_camera),
        ("case_internals.png", internal_scene(parts, args.tolerance, args.rear_roof_z), "Internal layout",
         "Front frame and display hidden; rear roof cut away for inspection", rear_camera),
        ("case_exploded.png", exploded_scene(parts), "Exploded assembly",
         "Actual CAD solids; layer separation added for presentation", (1.1, -1.4, 0.85)),
    )
    args.output_dir.mkdir(exist_ok=True, parents=True)
    preview = Image.new("RGB", (args.width, args.height), BACKGROUND)
    gutter = 12
    cell_w, cell_h = (args.width - gutter) // 2, (args.height - gutter) // 2
    for index, (filename, scene, title, subtitle, camera) in enumerate(views):
        small = view_image(scene, title, subtitle, camera, cell_w, cell_h, args.supersample)
        preview.paste(small, ((index % 2) * (cell_w + gutter), (index // 2) * (cell_h + gutter)))
        if not args.no_singles or filename == "case_exploded.png":
            full = view_image(scene, title, subtitle, camera, args.width, args.height, args.supersample)
            full.save(args.output_dir / filename)
            print(f"Saved {args.output_dir / filename}")
    preview.save(args.output_dir / "case_preview.png")
    print(f"Saved {args.output_dir / 'case_preview.png'}")
    print(f"Rendered {len(parts)} actual CAD objects / {sum(len(p.triangles) for p in parts)} facets")
    print(f"Sources: {case_path.name}, {stack_path.name}; {dimensions}")


if __name__ == "__main__":
    main()
