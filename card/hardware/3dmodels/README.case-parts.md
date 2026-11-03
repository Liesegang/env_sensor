# Card env sensor case reference parts

`CardEnvSensorCaseParts.FCStd` is the editable local mechanical parts library.
Run `build_case_parts.py` with FreeCAD's Python interpreter to regenerate the
three envelope STEP files. Existing vendor STEP files for SW1
(`SK-12D11VG3.step`), S1 (`LeverSwitch_TMHU27.step`), and the SFA40
sensor (`STEP_file_SFA40.stp`) are also included in the library document.

The panel model is the **bare display**, without Waveshare HAT. The battery is
the nominal 25 × 36 × 5 mm pouch body; its lead and seal tabs need placement
allowance in the case. The screw model is the nominal M3 × 5 shank and 6 mm
diameter, 0.7 mm high head; the head tolerance is ±0.15 mm. The thread and
driver recess are omitted because these files are collision envelopes.

The actual Fusion 3D PCB export is
`../case/generated/card_env_sensor_v2_fusion.step`. It includes off-board
footprints and many placeholder 3D packages, so use
`../case/generated/card_env_sensor_stack_reference.step` for enclosure
clearance against the panel, battery, screws, and two switches.
