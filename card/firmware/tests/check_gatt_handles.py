"""Check the actual nRF54L15 linked GATT database against legacy bond handles."""
from pathlib import Path
import re
import sys

build = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1] / "build"
link_map = (build / "zephyr/zephyr.map").read_text()
area = link_map.split("_bt_gatt_service_static_list_start = .", 1)[1].split(
    "_bt_gatt_service_static_list_end = .", 1
)[0]
names = re.findall(r"\._bt_gatt_service_static\.static\.([\w]+)_\s*$", area, re.MULTILINE)
assert names == ["_1_gatt_svc", "_2_gap_svc", "environment_service", "zz_card_image_service",
                 "zzz_card_history_service", "zzzz_env_clock_service"], names
handles = {}
next_handle = 1
for name in names:
    size = re.search(r"\.rodata\.attr_" + re.escape(name) +
                     r"\s*\n\s*0x[0-9a-f]+\s+0x([0-9a-f]+)", link_map)
    assert size, name
    # NCS 3.4.1 on 32-bit nRF54L15: struct bt_gatt_attr is 20 bytes.
    byte_count = int(size.group(1), 16)
    assert byte_count % 20 == 0, (name, byte_count)
    count = byte_count // 20
    handles[name] = list(range(next_handle, next_handle + count))
    next_handle += count
assert handles["environment_service"] == [16, 17, 18, 19], handles
assert handles["zz_card_image_service"] == list(range(20, 27)), handles
assert handles["zzz_card_history_service"] == list(range(27, 35)), handles
assert handles["zzzz_env_clock_service"] == list(range(35, 40)), handles
config = (build / "zephyr/.config").read_text()
assert "CONFIG_BT_GATT_SERVICE_CHANGED=y\n" in config
assert "CONFIG_BT_GATT_CACHING=y\n" in config
print("GATT handles PASS: legacy environment value=18 CCC=19; image control=22 data=24 status=26; history control=29 status=31 CCC=32 data=34; Service Changed/caching enabled")
