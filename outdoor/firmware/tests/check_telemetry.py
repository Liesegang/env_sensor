"""Validate the C serializer's bounds and generate shared receiver test vectors."""
import json
from pathlib import Path
import subprocess
import tempfile

app = Path(__file__).resolve().parents[1]
outdoor = app.parent
shared_driver = app.parents[1] / "shared/firmware/driver"
shared_telemetry = app.parents[1] / "shared/firmware/telemetry"
subprocess.run(["python3", str(outdoor / "protocol/generate.py"), "--check"], check=True)
with tempfile.TemporaryDirectory(prefix="outdoor-wire-") as temporary:
    output = Path(temporary) / "fixture"
    command = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(app / "telemetry/include"), "-I", str(shared_telemetry / "include")]
    for name in ("opt4001", "bme690", "sgp41", "stcc4", "bmv080", "as3935", "sfa40"):
        base = shared_driver if name in ("opt4001", "bme690", "sgp41", "stcc4", "sfa40") else app / "driver"
        command.extend(["-I", str(base / name / "include")])
    command.extend([str(shared_telemetry / "env_telemetry_encode.c"), str(app / "telemetry/telemetry_encode.c"), str(app / "tests/telemetry_fixture.c"), "-o", str(output)])
    subprocess.run(command, check=True)
    result = subprocess.run([str(output)], check=True, capture_output=True, text=True)
    vectors = json.loads(result.stdout)
    fixture = outdoor / "software/tests/fixtures/wire.json"
    fixture.parent.mkdir(parents=True, exist_ok=True)
    fixture.write_text(json.dumps(vectors, indent=2) + "\n")
    print(f'C encoder bounds + cross-language fixtures OK: {len(bytes.fromhex(vectors["all"]))} bytes (all sensors)')
