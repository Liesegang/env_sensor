"""Build the card v2 PCB mechanical reference from its Fusion/EAGLE board file.

Run with FreeCAD's bundled Python and FreeCAD's lib directory on PYTHONPATH.
The source board is read directly; no outline coordinates are copied by hand.
"""

from __future__ import annotations

import math
import csv
from pathlib import Path
import xml.etree.ElementTree as ET
import zipfile

import FreeCAD as App
import Part


HERE = Path(__file__).resolve().parent
BOARD_FILE = HERE.parent / "v2" / "card_env_sensor_1.fbrd"
THICKNESS = 1.6


def board_xml():
    with zipfile.ZipFile(BOARD_FILE) as archive:
        name = next(n for n in archive.namelist() if n.endswith(".brd"))
        return ET.fromstring(archive.read(name)).find("./drawing/board")


def vertex(x, y, z=0):
    return App.Vector(float(x), float(y), z)


def outline_edge(wire):
    a = vertex(wire.get("x1"), wire.get("y1"))
    b = vertex(wire.get("x2"), wire.get("y2"))
    sweep = math.radians(float(wire.get("curve", "0")))
    if abs(sweep) < 1e-10:
        return Part.makeLine(a, b)
    chord = b - a
    length = chord.Length
    center = (a + b) * 0.5 + App.Vector(-chord.y, chord.x, 0) * (1 / (2 * math.tan(sweep / 2)))
    start_angle = math.atan2(a.y - center.y, a.x - center.x)
    radius = length / (2 * abs(math.sin(sweep / 2)))
    mid = center + App.Vector(
        radius * math.cos(start_angle + sweep / 2),
        radius * math.sin(start_angle + sweep / 2),
        0,
    )
    return Part.Arc(a, mid, b).toShape()


def main():
    board = board_xml()
    segments = [
        item for item in board.find("plain")
        if item.tag == "wire" and item.get("layer") == "20"
    ]
    edges = [outline_edge(item) for item in segments]
    wires = Part.sortEdges(edges)
    if len(wires) != 1:
        raise ValueError(f"Expected one closed board outline, got {len(wires)}")
    outline = Part.Wire(wires[0])
    if not outline.isClosed():
        raise ValueError("Board outline is not closed")
    shape = Part.Face(outline).extrude(App.Vector(0, 0, -THICKNESS))

    holes = []
    for item in board.find("plain"):
        if item.tag == "hole":
            holes.append((float(item.get("x")), float(item.get("y")), float(item.get("drill"))))
    mounting_holes = list(holes)
    libraries = {
        (lib.get("name"), lib.get("urn", "")): lib
        for lib in board.findall("./libraries/library")
    }
    for element in board.findall("./elements/element"):
        library = libraries[(element.get("library"), element.get("library_urn", ""))]
        package = next(p for p in library.findall("./packages/package") if p.get("name") == element.get("package"))
        rotation = element.get("rot", "R0")
        angle = math.radians(float(rotation.split("R")[-1]))
        mirror = -1 if "M" in rotation else 1
        for item in package:
            if item.tag not in ("hole", "pad") or item.get("drill") is None:
                continue
            x, y = mirror * float(item.get("x", "0")), float(item.get("y", "0"))
            px = float(element.get("x")) + x * math.cos(angle) - y * math.sin(angle)
            py = float(element.get("y")) + x * math.sin(angle) + y * math.cos(angle)
            holes.append((px, py, float(item.get("drill"))))
    for via in board.findall("./signals/signal/via"):
        holes.append((float(via.get("x")), float(via.get("y")), float(via.get("drill"))))
    # Drills in parked documentation packages have no material to remove.
    tools = [
        Part.makeCylinder(diameter / 2, THICKNESS + 0.2, App.Vector(x, y, -THICKNESS - 0.1))
        for x, y, diameter in holes
        if shape.isInside(App.Vector(x, y, -THICKNESS / 2), 0.01, True)
    ]
    shape = shape.cut(Part.makeCompound(tools))
    shape = shape.removeSplitter()
    if not shape.isValid():
        raise ValueError("Invalid PCB solid")

    doc = App.newDocument("CardEnvSensorV2_PCB")
    pcb = doc.addObject("PartDesign::Feature", "PCB")
    pcb.Label = "PCB v2 - 1.6 mm"
    pcb.Shape = shape
    pcb.addProperty("App::PropertyString", "SourceBoard", "Source")
    pcb.SourceBoard = str(BOARD_FILE.relative_to(HERE.parents[2]))
    pcb.addProperty("App::PropertyLength", "BoardThickness", "Dimensions")
    pcb.BoardThickness = THICKNESS
    pcb.addProperty("App::PropertyString", "CoordinateNote", "Dimensions")
    pcb.CoordinateNote = "EAGLE XY; component side Z=0, solder side Z=-1.6 mm"
    if pcb.ViewObject is not None:
        pcb.ViewObject.ShapeColor = (0.04, 0.29, 0.25)
    doc.recompute()

    out = HERE / "generated"
    out.mkdir(exist_ok=True)
    doc.saveAs(str(out / "card_env_sensor_v2_pcb.FCStd"))
    Part.export([pcb], str(out / "card_env_sensor_v2_pcb.step"))
    with (out / "component_positions.csv").open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(("reference", "value", "package", "x_mm", "y_mm", "rotation", "on_board"))
        for element in board.find("elements"):
            x, y = float(element.get("x")), float(element.get("y"))
            writer.writerow((
                element.get("name"), element.get("value"), element.get("package"),
                x, y, element.get("rot", "R0"),
                shape.isInside(App.Vector(x, y, -THICKNESS / 2), 0.01, True),
            ))
    print(f"board: {shape.BoundBox.XLength:.3f} x {shape.BoundBox.YLength:.3f} x {shape.BoundBox.ZLength:.3f} mm")
    print(f"mounting holes: {mounting_holes}; total substrate drills: {len(tools)}")
    print(f"volume: {shape.Volume:.3f} mm^3")


if __name__ == "__main__":
    main()
