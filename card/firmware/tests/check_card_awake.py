#!/usr/bin/env python3
"""Guard the explicit always-awake configuration and absence of System Off calls."""
from pathlib import Path
import re

firmware = Path(__file__).resolve().parent.parent
config = (firmware / "prj.conf").read_text()
assert re.search(r"^CONFIG_PM=n\s*$", config, re.MULTILINE), "MCU PM must remain disabled"
assert re.search(r"^CONFIG_CARD_SWITCH_POLL_MS=50\s*$", config, re.MULTILINE), \
    "P2 push authorization requires the 50-ms observer"
for setting, expected in {"MIN_INT": 12, "MAX_INT": 24, "LATENCY": 0, "TIMEOUT": 600}.items():
    assert re.search(rf"^CONFIG_BT_PERIPHERAL_PREF_{setting}={expected}\s*$", config, re.MULTILINE), \
        f"Card transfer connection preference {setting} must remain {expected}"

off_calls = re.compile(
    r"\b(?:sys_poweroff|nrf_power_system_off|nrf_regulators_system_off|"
    r"nrf_power_system_off_set)\s*\(|\bPM_STATE_SOFT_OFF\b|\bSYSTEMOFF\s*="
)
for source in (firmware / "src").rglob("*.c"):
    code = re.sub(r"/\*.*?\*/|//[^\n]*", "", source.read_text(), flags=re.DOTALL)
    assert not off_calls.search(code), f"System Off call/state found in {source.name}"

print("Card awake configuration PASS: CONFIG_PM=n, 50-ms switch poll, BLE 15--30ms/latency0/timeout6s, no System Off calls")
