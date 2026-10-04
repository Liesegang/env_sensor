"""Check the built board against the schematic and PCB, without a target device."""

from pathlib import Path
import sys
import struct
import unittest
import xml.etree.ElementTree as ET
import zipfile

ncs_dir, build_dir = map(Path, sys.argv[1:3])
sys.path.insert(0, str(ncs_dir / "zephyr/scripts/dts/python-devicetree/src"))
from devicetree import dtlib
from elftools.elf.elffile import ELFFile

app_dir = Path(__file__).resolve().parents[1]
hardware_dir = app_dir.parent / "hardware"
dt = dtlib.DT(str(build_dir / "zephyr/zephyr.dts"))
config = dict(
    line.split("=", 1)
    for line in (build_dir / "zephyr/.config").read_text().splitlines()
    if line.startswith("CONFIG_")
)


def eagle_document(path, suffix):
    with zipfile.ZipFile(path) as archive:
        entry = next(name for name in archive.namelist() if name.endswith(suffix))
        return ET.fromstring(archive.read(entry))


schematic = eagle_document(hardware_dir / "env_sensor_3.fsch", ".sch")
pcb = eagle_document(hardware_dir / "env_sensor_3.fbrd", ".brd")


class BoardBuildTests(unittest.TestCase):
    def test_buzzer_matches_schematic_and_pcb(self):
        mcu = schematic.find(".//parts/part[@name='IC1']")
        self.assertEqual(mcu.get("deviceset"), "ES4L15BA1")
        refs = schematic.findall(".//nets/net[@name='BUZZER']//pinref")
        self.assertEqual(
            {(ref.get("part"), ref.get("pin")) for ref in refs},
            {("IC1", "P1.01/XL2"), ("R1", "1")},
        )
        pads = set()
        for ref in refs:
            part = schematic.find(f".//parts/part[@name='{ref.get('part')}']")
            connect = schematic.find(
                f".//library[@name='{part.get('library')}']/devicesets/"
                f"deviceset[@name='{part.get('deviceset')}']/devices/"
                f"device[@name='{part.get('device')}']/connects/"
                f"connect[@gate='{ref.get('gate')}'][@pin='{ref.get('pin')}']"
            )
            self.assertIsNotNone(connect)
            pads.add((ref.get("part"), connect.get("pad")))
        pcb_refs = pcb.findall(".//signals/signal[@name='BUZZER']/contactref")
        self.assertEqual({(r.get("element"), r.get("pad")) for r in pcb_refs}, pads)

    def test_pwm_drives_only_p1_01_active_high(self):
        buzzer = dt.get_node("buzzer")
        phandle, channel, period, flags = struct.unpack(">4I", buzzer.props["pwms"].value)
        pwm = dt.phandle2node[phandle]
        self.assertEqual(pwm, dt.label2node["pwm20"])
        self.assertEqual((channel, period, flags), (0, 500_000, 0))
        self.assertEqual(pwm.props["status"].to_string(), "okay")
        pins = pwm.props["pinctrl-0"].to_node()
        self.assertEqual(len(pins.nodes), 1)
        group = next(iter(pins.nodes.values()))
        # NRF_PSEL encodes function in bits 24..31 and port*32+pin in bits 0..8.
        self.assertEqual(group.props["psels"].to_nums(), [(22 << 24) | 33])
        self.assertNotIn("nordic,invert", group.props)
        for node in dt.node_iter():
            if "compatible" not in node.props:
                continue
            compat = node.props["compatible"].to_strings()
            if "nordic,nrf-pwm" in compat and node != pwm:
                self.assertEqual(node.props["status"].to_string(), "disabled")

    def test_gpio_clock_and_power_do_not_conflict(self):
        self.assertEqual(dt.label2node["lfxo"].props["status"].to_string(), "disabled")
        self.assertEqual(config["CONFIG_CLOCK_CONTROL_NRF_K32SRC_RC"], "y")
        self.assertEqual(config["CONFIG_NRF_GRTC_TIMER_SOURCE_SYSTEM_LFCLK"], "y")
        self.assertNotIn("CONFIG_CLOCK_CONTROL_NRF_K32SRC_XTAL", config)
        self.assertEqual(
            dt.label2node["vregmain"].props["regulator-initial-mode"].to_num(), 1
        )
        self.assertNotIn("CONFIG_SERIAL", config)
        self.assertNotIn("CONFIG_UART_CONSOLE", config)

    def test_buzzer_and_debug_configuration(self):
        duty = int(config["CONFIG_BUZZER_DUTY_PERMILLE"])
        self.assertGreater(duty, 0)
        self.assertLessEqual(duty, 500)
        if config.get("CONFIG_APP_BUZZER_TEST") == "y":
            self.assertLessEqual(int(config["CONFIG_BUZZER_ON_MS"]), 100)
            self.assertGreaterEqual(int(config["CONFIG_BUZZER_INTERVAL_MS"]), 1000)
            self.assertGreater(int(config["CONFIG_BUZZER_BEEP_COUNT"]), 0)
        self.assertEqual(config["CONFIG_RTT_CONSOLE"], "y")
        self.assertEqual(config["CONFIG_SEGGER_RTT_MODE_NO_BLOCK_SKIP"], "y")
        self.assertEqual(config["CONFIG_NRF_APPROTECT_DISABLE"], "y")
        self.assertEqual(config["CONFIG_PWM"], "y")

    def test_ble_uses_the_board_entropy_source_and_calibrated_rc_clock(self):
        if config.get("CONFIG_APP_BLE") != "y":
            return
        self.assertEqual(config["CONFIG_BT"], "y")
        self.assertEqual(config["CONFIG_BT_PERIPHERAL"], "y")
        self.assertEqual(config["CONFIG_BT_MAX_CONN"], "1")
        self.assertGreaterEqual(int(config["CONFIG_BT_L2CAP_TX_MTU"]), 247)
        self.assertEqual(config["CONFIG_CLOCK_CONTROL_NRF_K32SRC_RC_CALIBRATION"], "y")
        self.assertEqual(config["CONFIG_CLOCK_CONTROL_NRF_K32SRC_500PPM"], "y")
        self.assertEqual(dt.label2node["hfxo"].props["load-capacitors"].to_string(), "internal")
        self.assertEqual(dt.label2node["hfxo"].props["load-capacitance-femtofarad"].to_num(), 15000)
        entropy = dt.get_node("/chosen").props["zephyr,entropy"].to_path()
        self.assertEqual(entropy, dt.label2node["psa_rng"])
        self.assertEqual(entropy.props["status"].to_string(), "okay")

    def test_i2c_pins_match_schematic_and_pcb(self):
        for net_name, mcu_pin in (("SCL", "P0.03"), ("SDA", "P0.04")):
            refs = schematic.findall(f".//nets/net[@name='{net_name}']//pinref")
            mcu_ref = next(ref for ref in refs if ref.get("part") == "IC1")
            self.assertEqual(mcu_ref.get("pin"), mcu_pin)
            part = schematic.find(".//parts/part[@name='IC1']")
            connect = schematic.find(
                f".//library[@name='{part.get('library')}']/devicesets/"
                f"deviceset[@name='{part.get('deviceset')}']/devices/"
                f"device[@name='{part.get('device')}']/connects/"
                f"connect[@gate='{mcu_ref.get('gate')}'][@pin='{mcu_pin}']"
            )
            pcb_ref = pcb.find(
                f".//signals/signal[@name='{net_name}']/contactref[@element='IC1']"
            )
            self.assertEqual(pcb_ref.get("pad"), connect.get("pad"))

    def test_i2c_uses_twim30_on_port_zero(self):
        bus = dt.get_node("i2c-sensors")
        self.assertEqual(bus, dt.label2node["i2c30"])
        self.assertEqual(bus.props["status"].to_string(), "okay")
        self.assertEqual(bus.props["clock-frequency"].to_num(), 100_000)
        self.assertIn("nordic,nrf-twim", bus.props["compatible"].to_strings())
        group = next(iter(bus.props["pinctrl-0"].to_node().nodes.values()))
        self.assertEqual(group.props["psels"].to_nums(), [(11 << 24) | 3, (12 << 24) | 4])
        self.assertEqual(config["CONFIG_I2C"], "y")
        self.assertEqual(config["CONFIG_APP_I2C_SCAN"], "y")
        self.assertGreater(int(config["CONFIG_I2C_NRFX_TRANSFER_TIMEOUT"]), 0)
        for label in ("spi30", "uart30"):
            self.assertEqual(dt.label2node[label].props["status"].to_string(), "disabled")

    def test_sensor_nodes_use_existing_and_planned_addresses(self):
        bus = dt.label2node["i2c30"]
        for label, address in (("opt4001", 0x45), ("sgp41", 0x59),
                               ("stcc4", 0x64), ("bme690", 0x76), ("bmv080", 0x54),
                               ("as3935", 0x03), ("sfa40", 0x5d)):
            node = dt.label2node[label]
            self.assertEqual(node.parent, bus)
            self.assertEqual(node.props["reg"].to_num(), address)
        self.assertEqual(config["CONFIG_APP_SENSOR_READ"], "y")

    def test_parts_overrides_match_the_sensor_drivers(self):
        # PARTS describes populated parts; library devicesets/packages may be reused.
        for reference, value in (("IC5", "bme690"), ("IC6", "spg41"),
                                  ("IC7", "SHT4x-Axx")):
            part = schematic.find(f".//parts/part[@name='{reference}']")
            self.assertEqual(part.find("attribute[@name='PARTS']").get("value"), value)
            element = pcb.find(f".//elements/element[@name='{reference}']")
            self.assertEqual(element.find("attribute[@name='PARTS']").get("value"), value)
        self.assertEqual(dt.label2node["sgp41"].props["compatible"].to_strings(),
                         ["sensirion,sgp41"])
        self.assertEqual(dt.label2node["bme690"].props["compatible"].to_strings(),
                         ["bosch,bme690"])
        self.assertEqual(schematic.find(".//parts/part[@name='IC9']").get("value"), "BMV080")
        self.assertEqual(dt.label2node["bmv080"].props["compatible"].to_strings(),
                         ["bosch,bmv080"])
        self.assertEqual(schematic.find(".//parts/part[@name='IC3']").get("value"), "SFA40PIN2")

    def test_future_sensor_initializers_follow_configuration(self):
        with (build_dir / "zephyr/zephyr.elf").open("rb") as stream:
            elf = ELFFile(stream)
            names = {symbol.name for symbol in elf.get_section_by_name(".symtab").iter_symbols()}
        for option, symbol in (("APP_AS3935", "as3935_init"),
                               ("APP_SFA40", "sfa40_init"),
                               ("APP_BMV080", "bmv080_driver_init")):
            self.assertEqual(symbol in names, config.get("CONFIG_" + option) == "y")

    def test_sd_spi_and_non_destructive_mount(self):
        if config.get("CONFIG_APP_SD_LOG") != "y":
            return
        self.assertIn("nfct-pins-as-gpios", dt.label2node["uicr"].props)
        spi = dt.label2node["spi20"]
        self.assertEqual(spi.props["status"].to_string(), "okay")
        groups = spi.props["pinctrl-0"].to_node().nodes.values()
        self.assertEqual({pin for group in groups for pin in group.props["psels"].to_nums()},
                         {(4 << 24) | 36, (5 << 24) | 38, (6 << 24) | 34})
        cs = struct.unpack(">3I", spi.props["cs-gpios"].value)
        self.assertEqual(dt.phandle2node[cs[0]], dt.label2node["gpio2"])
        self.assertEqual(cs[1:], (4, 1))
        self.assertEqual(dt.label2node["sd_card"].props["disk-name"].to_string(), "SD")
        self.assertNotIn("CONFIG_FS_FATFS_MOUNT_MKFS", config)
        self.assertNotIn("CONFIG_FS_FATFS_MKFS", config)
        self.assertEqual(config["CONFIG_FS_FATFS_HAS_RTC"], "y")
        for net, part, pin in (("MOSI", "IC1", "P1.06/AIN2"),
                               ("MISO", "IC1", "P1.02/NFC1"),
                               ("CS_SD_N", "IC1", "P2.04"),
                               ("N$1", "IC1", "P1.04/AIN0"),
                               ("N$1", "R3", "1"), ("CLK", "R3", "2"),
                               ("CLK", "SD1", "CLK")):
            self.assertIsNotNone(schematic.find(f".//nets/net[@name='{net}']//pinref[@part='{part}'][@pin='{pin}']"))

    def test_arm_firmware_and_rtt_symbols_exist(self):
        for suffix in ("elf", "hex", "bin"):
            self.assertGreater((build_dir / f"zephyr/zephyr.{suffix}").stat().st_size, 0)
        with (build_dir / "zephyr/zephyr.elf").open("rb") as stream:
            elf = ELFFile(stream)
            self.assertEqual(elf.get_machine_arch(), "ARM")
            names = {symbol.name for symbol in elf.get_section_by_name(".symtab").iter_symbols()}
            self.assertTrue({"main", "_SEGGER_RTT"}.issubset(names))
            if config.get("CONFIG_APP_BUZZER_TEST") != "y":
                self.assertNotIn("buzzer_start", names)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]], verbosity=2)
