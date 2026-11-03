"""Recover the v2 PCB's used Fusion libraries without merging different origins.

The embedded files are Fusion/EAGLE XML 9.7.0. Keep that format and the original
symbols, pad geometry, pin mappings and 3D package links. Each source library
gets a distinct local name so the cloud and local ``dac`` definitions cannot
overwrite each other.
"""

from __future__ import annotations

from collections import defaultdict
from copy import deepcopy
from pathlib import Path
import xml.etree.ElementTree as ET
import zipfile


HERE = Path(__file__).resolve().parent
SOURCE = HERE.parent / "v2"
OUTPUT = HERE / "fusion_libraries"
LIBRARY_METADATA = {"urn", "library_urn", "library_version"}
THREE_D_TAGS = {"packages3d", "package3dinstances"}


def embedded_xml(path: Path, suffix: str) -> ET.Element:
    with zipfile.ZipFile(path) as archive:
        matches = [name for name in archive.namelist() if name.endswith(suffix)]
        if len(matches) != 1:
            raise ValueError(f"Expected one {suffix} document in {path}: {matches}")
        return ET.fromstring(archive.read(matches[0]))


def origin(node: ET.Element, reference: bool = False) -> tuple[str, str]:
    return (
        node.get("library" if reference else "name", ""),
        node.get("library_urn" if reference else "urn", ""),
    )


def output_name(key: tuple[str, str]) -> str:
    name, urn = key
    if name == "dac":
        return "card_dac_3d" if urn else "card_dac_local_3d"
    if name == "SamacSys_Parts" and urn:
        return "card_samac_3d"
    raise ValueError(f"Unexpected source library origin: {key}")


def named(children: ET.Element | None, context: str) -> dict[str, ET.Element]:
    result = {}
    for child in children if children is not None else []:
        name = child.get("name", "")
        if name in result:
            raise ValueError(f"Duplicate asset {context}/{name}")
        result[name] = child
    return result


def remove_library_metadata(node: ET.Element) -> None:
    # 3D package URNs, version data and packageinstance links remain intact.
    if node.tag in THREE_D_TAGS:
        return
    for key in LIBRARY_METADATA:
        node.attrib.pop(key, None)
    for child in node:
        remove_library_metadata(child)


def definition(node: ET.Element, keep_metadata: bool = False) -> bytes:
    copy = deepcopy(node)
    if not keep_metadata:
        remove_library_metadata(copy)
    for child in copy.iter():
        child.attrib = dict(sorted(child.attrib.items()))
        if child.text is not None and not child.text.strip():
            child.text = None
        child.tail = None
    return ET.tostring(copy, encoding="utf-8")


def require_equal(a: ET.Element, b: ET.Element, label: str) -> None:
    if definition(a) != definition(b):
        raise ValueError(f"Different asset definitions: {label}; refusing overwrite")


def make_library(
    key: tuple[str, str], sch_lib: ET.Element, pcb_lib: ET.Element,
    parts: list[ET.Element], elements: list[ET.Element],
) -> ET.Element:
    used_packages = {element.get("package", "") for element in elements}
    sch_packages = named(sch_lib.find("packages"), f"{key}/SCH packages")
    pcb_packages = named(pcb_lib.find("packages"), f"{key}/PCB packages")
    for package in sorted(used_packages):
        if package not in sch_packages or package not in pcb_packages:
            raise ValueError(f"Missing used package {key}/{package}")
        require_equal(sch_packages[package], pcb_packages[package], f"{key}/{package}")

    used_variants = defaultdict(set)
    for part in parts:
        used_variants[part.get("deviceset", "")].add(part.get("device", ""))
    source_sets = named(sch_lib.find("devicesets"), f"{key}/devicesets")
    source_symbols = named(sch_lib.find("symbols"), f"{key}/symbols")
    selected_sets = []
    used_symbols = set()
    for set_name, variants in used_variants.items():
        if set_name not in source_sets:
            raise ValueError(f"Missing deviceset {key}/{set_name}")
        deviceset = deepcopy(source_sets[set_name])
        devices = deviceset.find("devices")
        source_devices = named(devices, f"{key}/{set_name}/devices")
        if not variants <= source_devices.keys():
            raise ValueError(f"Missing variants {key}/{set_name}: {variants - source_devices.keys()}")
        for device in list(devices):
            if device.get("name", "") not in variants:
                devices.remove(device)
            elif device.get("package") and device.get("package") not in used_packages:
                raise ValueError(f"SCH variant absent from PCB: {key}/{set_name}/{device.attrib}")
        for gate in deviceset.find("gates"):
            used_symbols.add(gate.get("symbol", ""))
        selected_sets.append(deviceset)

    result = ET.Element("library", {"name": output_name(key)})
    if sch_lib.find("description") is not None:
        result.append(deepcopy(sch_lib.find("description")))
    packages = ET.SubElement(result, "packages")
    for package in sch_lib.find("packages"):
        if package.get("name") in used_packages:
            packages.append(deepcopy(package))
    if sch_lib.find("packages3d") is not None:
        packages3d = ET.SubElement(result, "packages3d")
        for package3d in sch_lib.find("packages3d"):
            refs = {instance.get("name") for instance in package3d.findall("./packageinstances/packageinstance")}
            if refs & used_packages:
                if not refs <= used_packages:
                    raise ValueError(f"3D package references unused footprints: {refs - used_packages}")
                packages3d.append(deepcopy(package3d))
    symbols = ET.SubElement(result, "symbols")
    for symbol in sch_lib.find("symbols"):
        if symbol.get("name") in used_symbols:
            symbols.append(deepcopy(symbol))
    if not used_symbols <= source_symbols.keys():
        raise ValueError(f"Missing gate symbols {key}: {used_symbols - source_symbols.keys()}")
    if sch_lib.find("atts") is not None:
        result.append(deepcopy(sch_lib.find("atts")))
    devicesets = ET.SubElement(result, "devicesets")
    devicesets.extend(selected_sets)
    remove_library_metadata(result)
    return result


def validate_library(
    key: tuple[str, str], library: ET.Element, source: ET.Element,
    parts: list[ET.Element], elements: list[ET.Element],
) -> None:
    packages = named(library.find("packages"), "output packages")
    symbols = named(library.find("symbols"), "output symbols")
    sets = named(library.find("devicesets"), "output devicesets")
    if set(packages) != {element.get("package") for element in elements}:
        raise ValueError(f"Output package set does not match PCB use: {key}")
    source_packages = named(source.find("packages"), "source packages")
    source_symbols = named(source.find("symbols"), "source symbols")
    source_sets = named(source.find("devicesets"), "source devicesets")
    for name, package in packages.items():
        require_equal(package, source_packages[name], f"unchanged package {key}/{name}")
    for name, symbol in symbols.items():
        require_equal(symbol, source_symbols[name], f"unchanged symbol {key}/{name}")
    by_reference = {element.get("name"): element for element in elements}
    for part in parts:
        set_name, device_name = part.get("deviceset"), part.get("device", "")
        deviceset = sets[set_name]
        device = named(deviceset.find("devices"), "devices")[device_name]
        source_device = named(source_sets[set_name].find("devices"), "source devices")[device_name]
        require_equal(device, source_device, f"unchanged device/connects {key}/{set_name}/{device_name}")
        require_equal(deviceset.find("gates"), source_sets[set_name].find("gates"), f"unchanged gates {key}/{set_name}")
        for gate in deviceset.find("gates"):
            if gate.get("symbol") not in symbols:
                raise ValueError(f"Unresolved gate symbol: {gate.attrib}")
        package = device.get("package")
        if package:
            element = by_reference.get(part.get("name"))
            if element is None or element.get("package") != package:
                raise ValueError(f"PCB/SCH part package mismatch: {key}/{part.get('name')}")
        elif part.get("name") in by_reference:
            raise ValueError(f"Package-less SCH part unexpectedly on PCB: {part.get('name')}")
    if {part.get("name") for part in parts if part.get("name") in by_reference} != set(by_reference):
        raise ValueError(f"PCB elements absent from schematic: {key}")
    source_3d = named(source.find("packages3d"), "source packages3d")
    for package3d in library.findall("./packages3d/package3d"):
        if definition(package3d, True) != definition(source_3d[package3d.get("name")], True):
            raise ValueError(f"3D package metadata changed: {key}/{package3d.get('name')}")


def main() -> None:
    sch_root = embedded_xml(SOURCE / "card_env_sensor_1.fsch", ".sch")
    pcb_root = embedded_xml(SOURCE / "card_env_sensor_1.fbrd", ".brd")
    sch = sch_root.find("./drawing/schematic")
    pcb = pcb_root.find("./drawing/board")
    sch_libraries = {origin(lib): lib for lib in sch.find("libraries")}
    pcb_libraries = {origin(lib): lib for lib in pcb.find("libraries")}
    if len(sch_libraries) != len(sch.find("libraries")):
        raise ValueError("Duplicate schematic library origin")
    if len(pcb_libraries) != len(pcb.find("libraries")):
        raise ValueError("Duplicate PCB library origin")
    parts_by_origin, elements_by_origin = defaultdict(list), defaultdict(list)
    for part in sch.find("parts"):
        parts_by_origin[origin(part, True)].append(part)
    for element in pcb.find("elements"):
        elements_by_origin[origin(element, True)].append(element)
    if not (parts_by_origin.keys() | elements_by_origin.keys()) <= sch_libraries.keys() & pcb_libraries.keys():
        raise ValueError("A part or PCB element references a missing library origin")
    names = [output_name(key) for key in parts_by_origin]
    if len(names) != len(set(names)):
        raise ValueError("Multiple source origins map to the same output; refusing merge")

    prepared = []
    for key in parts_by_origin:
        parts, elements = parts_by_origin[key], elements_by_origin[key]
        library = make_library(key, sch_libraries[key], pcb_libraries[key], parts, elements)
        validate_library(key, library, sch_libraries[key], parts, elements)
        root = ET.Element("eagle", {"version": sch_root.get("version")})
        drawing = ET.SubElement(root, "drawing")
        for tag in ("settings", "grid", "layers"):
            child = sch_root.find(f"./drawing/{tag}")
            if child is not None:
                drawing.append(deepcopy(child))
        drawing.append(library)
        ET.indent(root, space="  ")
        xml = '<?xml version="1.0" encoding="utf-8"?>\n<!DOCTYPE eagle SYSTEM "eagle.dtd">\n'
        xml += ET.tostring(root, encoding="unicode") + "\n"
        parsed = ET.fromstring(xml)
        # Indentation is immaterial, but repeat reference/geometry validation on
        # the serialized document to catch accidental emission changes.
        emitted = parsed.find("./drawing/library")
        validate_library(key, emitted, sch_libraries[key], parts, elements)
        prepared.append((output_name(key), xml, len(parts), len(elements), library))

    OUTPUT.mkdir(exist_ok=True)
    for name, xml, part_count, element_count, library in prepared:
        path = OUTPUT / f"{name}.lbr"
        path.write_text(xml, encoding="utf-8")
        ET.parse(path)
        print(f"{path.name}: {part_count} SCH parts, {element_count} PCB elements, "
              f"{len(library.find('packages'))} packages, {len(library.find('symbols'))} symbols, "
              f"{len(library.find('devicesets'))} devicesets; all source definitions validated")
    print(f"PASS: {len(prepared)} distinct origins, all {len(sch.find('parts'))} schematic parts "
          f"and {len(pcb.find('elements'))} PCB elements resolved; package geometry, symbols, "
          "gates, device pin mappings and existing 3D links preserved")


if __name__ == "__main__":
    main()
