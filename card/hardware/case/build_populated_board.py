"""Export the populated board reference without enclosure assembly parts."""

from pathlib import Path

import FreeCAD as App
import Part

from build_pcb_model import board_xml


OUT = Path(__file__).resolve().parent / "generated"


def main():
    source = App.openDocument(str(OUT / "card_env_sensor_stack_reference.FCStd"))
    names = ["PCB"] + [element.get("name") for element in board_xml().find("elements")]
    doc = App.newDocument("CardEnvSensorV2_PopulatedBoard")
    exported = []
    for name in names:
        original = source.getObject(name)
        if original is None:
            continue
        obj = doc.addObject("PartDesign::Feature", name)
        obj.Label = original.Label
        obj.Shape = original.Shape.copy()
        for key in ("MechanicalSource", "PopulationAssumption"):
            if hasattr(original, key):
                obj.addProperty("App::PropertyString", key, "Reference")
                setattr(obj, key, getattr(original, key))
        exported.append(obj)
    doc.recompute()
    doc.saveAs(str(OUT / "card_env_sensor_v2_populated.FCStd"))
    Part.export(exported, str(OUT / "card_env_sensor_v2_populated.step"))
    print(f"Populated board reference: PCB + {len(exported) - 1} component references")


if __name__ == "__main__":
    main()
