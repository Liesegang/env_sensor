"""Remove documentation footprints placed outside the v2 Fusion 3D PCB.

The source is a direct Fusion STEP export. It contains a few off-board parts
that are parked below the PCB in the electronic layout. The kept geometry is
selected by solid center in an expanded board XY region, without altering it.
"""

from pathlib import Path

import FreeCAD as App
import Part


OUT = Path(__file__).resolve().parent / "generated"


def main():
    source = Part.read(str(OUT / "card_env_sensor_v2_fusion.step"))
    kept = []
    excluded = []
    for solid in source.Solids:
        center = solid.CenterOfMass
        if -46.0 <= center.x <= 47.0 and -30.0 <= center.y <= 30.0:
            kept.append(solid)
        else:
            excluded.append(solid)
    result = Part.makeCompound(kept)
    if not result.isValid():
        raise ValueError("Filtered Fusion STEP has invalid geometry")
    doc = App.newDocument("CardEnvSensor_FusionOnboard")
    obj = doc.addObject("PartDesign::Feature", "FusionOnboard")
    obj.Shape = result
    Part.export([obj], str(OUT / "card_env_sensor_v2_fusion_onboard.step"))
    print(f"Fusion solids: {len(source.Solids)}; kept: {len(kept)}; off-board: {len(excluded)}")
    print(f"on-board bbox: {result.BoundBox}")


if __name__ == "__main__":
    main()
