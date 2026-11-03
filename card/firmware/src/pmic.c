#include "pmic.h"

#include <errno.h>
#include <stdbool.h>

#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#define NPM1300_ADDRESS 0x6b
#define VBUS_BASE 0x02
#define VBUS_STATUS 0x07
#define VBUS_SUSPEND 0x03
#define VBUS_CC_DETECTION 0x05
#define VBUS_PRESENT BIT(0)
#define CHARGER_BASE 0x03
#define CHARGER_RELEASE_ERROR 0x00
#define CHARGER_CLEAR_ERROR_REGISTERS 0x01
#define CHARGER_ENABLE 0x04
#define CHARGER_ENABLE_CLEAR 0x05
#define CHARGER_DISABLE 0x06
#define CHARGER_CURRENT_MSB 0x08
#define CHARGER_CURRENT_LSB 0x09
#define CHARGER_TERMINATION_VOLTAGE 0x0c
#define CHARGER_WARM_TERMINATION_VOLTAGE 0x0d
#define CHARGER_STATUS 0x34
#define CHARGER_ERROR 0x36
#define CHARGER_ENABLED BIT(0)
#define CHARGER_NTC_DISABLED BIT(1)
#define BUCK_BASE 0x04
#define BUCK1_ENABLE 0x00
#define BUCK2_AUTO_MODE 0x07
#define BUCK1_NORMAL_VOLTAGE 0x08
#define BUCK_SOFTWARE_CONTROL 0x0f
#define BUCK_GPIO_ENABLE_CONTROL 0x0c
#define BUCK1_VOLTAGE_STATUS 0x10
#define BUCK2_VOLTAGE_STATUS 0x11
#define BUCK_STATUS 0x34
#define BUCK1_SOFTWARE_CONTROL BIT(0)
#define BUCK1_POWER_GOOD BIT(2)
#define BUCK_VOLTAGE_3V3 23
#define ADC_BASE 0x05
#define ADC_MEASURE_VBAT 0x00
#define ADC_MEASURE_VSYS 0x03
#define ADC_MEASURE_VBUS 0x07
#define ADC_CONFIGURATION 0x09
#define ADC_NTC_RESISTOR_SELECTION 0x0a
#define ADC_UPDATE_AUTO_MEASUREMENT 0x0c
#define ADC_VBAT_MSB 0x11
#define ADC_VSYS_MSB 0x14
#define ADC_RESULT_LSB 0x15
#define ADC_BURST_VBAT0_MSB 0x16
#define ADC_VBUS_MSB 0x19
#define ADC_BURST_RESULT_LSB 0x1a
#define ADC_CURRENT_MEASUREMENT_ENABLE 0x24
#define ADC_CURRENT_MEASUREMENT_ENABLED BIT(0)
#define ADC_BURST_ENABLED BIT(1)
#define LED_BASE 0x0a
#define LED1_MODE 0x01
#define LED_MODE_MASK 0x03
#define LED_MODE_CHARGING 1
#define SHIP_BASE 0x0b
#define SHIP_PIN_STATUS 0x05

static const struct i2c_dt_spec pmic = {
	.bus = DEVICE_DT_GET(DT_NODELABEL(i2c30)),
	.addr = NPM1300_ADDRESS,
};

static int read_register(uint8_t base, uint8_t offset, uint8_t *value)
{
	uint8_t address[] = {base, offset};
	int err = i2c_write_read_dt(&pmic, address, sizeof(address), value, 1);

	if (err != 0) {
		printk("PMIC: read 0x%02x%02x failed: %d\n", base, offset, err);
	}
	return err;
}

static int write_register(uint8_t base, uint8_t offset, uint8_t value)
{
	uint8_t bytes[] = {base, offset, value};
	int err = i2c_write_dt(&pmic, bytes, sizeof(bytes));

	if (err != 0) {
		printk("PMIC: write 0x%02x%02x failed: %d\n", base, offset, err);
	}
	return err;
}

static int configure_charging_led(void)
{
	uint8_t mode;
	int err = read_register(LED_BASE, LED1_MODE, &mode);

	if (err != 0) {
		return err;
	}
	uint8_t expected = (mode & ~LED_MODE_MASK) | LED_MODE_CHARGING;

	/* Board LED1: anode VSYS, cathode nPM1300 LED1, a 5 mA current sink.
	 * CHARGING mode follows the charger state without MCU polling.
	 */
	err = write_register(LED_BASE, LED1_MODE, expected);
	if (err != 0) {
		return err;
	}
	err = read_register(LED_BASE, LED1_MODE, &mode);
	if (err != 0) {
		return err;
	}
	if (mode != expected) {
		printk("PMIC: LED1 mode readback mismatch: 0x%02x\n", mode);
		return -EIO;
	}
	printk("PMIC: LED1 automatic charging indication READY\n");
	return 0;
}

static int log_buck_status(void)
{
	static const uint8_t offsets[] = {
		BUCK1_NORMAL_VOLTAGE, BUCK_SOFTWARE_CONTROL, BUCK_GPIO_ENABLE_CONTROL,
		BUCK1_VOLTAGE_STATUS, BUCK2_VOLTAGE_STATUS, BUCK_STATUS,
	};
	uint8_t values[ARRAY_SIZE(offsets)];

	for (size_t i = 0; i < ARRAY_SIZE(offsets); i++) {
		int err = read_register(BUCK_BASE, offsets[i], &values[i]);

		if (err != 0) {
			return err;
		}
	}
	/* ENASET is a write-only task. GPIO and voltage controls are readable. */
	printk("PMIC: BUCK norm1=0x%02x sw_ctrl=0x%02x gpio_ctrl=0x%02x "
	       "target1=%u target2=%u status=0x%02x\n",
	       values[0], values[1], values[2], values[3], values[4], values[5]);
	return 0;
}

static int configure_sensor_supply_auto(void)
{
	/* Keep BUCK2 in Auto mode, including after a warm MCU reset following
	 * bring-up. Forced PWM raised VSYS during the SWD-connected test.
	 */
	int err = write_register(BUCK_BASE, BUCK2_AUTO_MODE, 1);

	if (err != 0) {
		return err;
	}
	k_msleep(5);
	printk("PMIC: BUCK2 Auto mode selected; voltage unchanged\n");
	return log_buck_status();
}

static int power_on_display(void)
{
	uint8_t original_control;
	uint8_t original_buck2_target;
	uint8_t control;
	uint8_t buck1_target;
	uint8_t buck2_target;
	uint8_t status;
	int err;

	err = read_register(BUCK_BASE, BUCK_SOFTWARE_CONTROL, &original_control);
	if (err != 0) {
		return err;
	}
	err = read_register(BUCK_BASE, BUCK2_VOLTAGE_STATUS, &original_buck2_target);
	if (err != 0) {
		return err;
	}
	/* VSET1 is grounded on v2, so BUCK1 starts off. Set its voltage first.
	 * This follows regulator_npm13xx.c and preserves BUCK2's control bit.
	 */
	err = write_register(BUCK_BASE, BUCK1_NORMAL_VOLTAGE, BUCK_VOLTAGE_3V3);
	if (err != 0) {
		return err;
	}
	err = write_register(BUCK_BASE, BUCK_SOFTWARE_CONTROL,
			     original_control | BUCK1_SOFTWARE_CONTROL);
	if (err != 0) {
		return err;
	}
	err = write_register(BUCK_BASE, BUCK1_ENABLE, 1);
	if (err != 0) {
		return err;
	}
	(void)log_buck_status();

	int64_t deadline = k_uptime_get() + 100;

	do {
		err = read_register(BUCK_BASE, BUCK_STATUS, &status);
		if (err != 0) {
			return err;
		}
		if ((status & BUCK1_POWER_GOOD) != 0) {
			break;
		}
		k_msleep(5);
	} while (k_uptime_get() < deadline);
	if ((status & BUCK1_POWER_GOOD) == 0) {
		printk("PMIC: BUCK1 PWRGOOD timeout, status=0x%02x\n", status);
		return -ETIMEDOUT;
	}
	err = read_register(BUCK_BASE, BUCK_SOFTWARE_CONTROL, &control);
	if (err != 0) {
		return err;
	}
	err = read_register(BUCK_BASE, BUCK1_VOLTAGE_STATUS, &buck1_target);
	if (err != 0) {
		return err;
	}
	err = read_register(BUCK_BASE, BUCK2_VOLTAGE_STATUS, &buck2_target);
	if (err != 0) {
		return err;
	}
	if (control != (original_control | BUCK1_SOFTWARE_CONTROL) ||
	    buck1_target != BUCK_VOLTAGE_3V3 || buck2_target != original_buck2_target) {
		printk("PMIC: BUCK readback mismatch ctrl=0x%02x BUCK1=%u BUCK2=%u/%u\n",
		       control, buck1_target, buck2_target, original_buck2_target);
		return -EIO;
	}
	printk("PMIC: display BUCK1 3.3 V READY, status=0x%02x; BUCK2 target=%u\n",
	       status, buck2_target);
	return 0;
}

static int print_power_status(void)
{
	uint8_t usb;
	uint8_t suspend;
	uint8_t cc_detection;
	uint8_t ship_pin;
	uint8_t enable;
	uint8_t disable;
	uint8_t charge_status;
	uint8_t charge_error;
	uint8_t current_msb;
	uint8_t current_lsb;
	uint8_t termination;
	uint8_t adc_configuration;
	uint8_t vbat_msb;
	uint8_t vbat_lsb;
	uint8_t vsys_msb;
	uint8_t vsys_lsb;
	uint8_t vbus_msb;
	uint8_t vbus_lsb;
	int err;

	/* Read existing charger configuration. No chemistry/current assumptions. */
	err = read_register(VBUS_BASE, VBUS_STATUS, &usb);
	if (err != 0) {
		return err;
	}
	err = read_register(VBUS_BASE, VBUS_SUSPEND, &suspend);
	if (err != 0) {
		return err;
	}
	err = read_register(VBUS_BASE, VBUS_CC_DETECTION, &cc_detection);
	if (err != 0) {
		return err;
	}
	err = read_register(SHIP_BASE, SHIP_PIN_STATUS, &ship_pin);
	if (err != 0) {
		return err;
	}
	err = read_register(CHARGER_BASE, CHARGER_ENABLE, &enable);
	if (err != 0) {
		return err;
	}
	err = read_register(CHARGER_BASE, CHARGER_DISABLE, &disable);
	if (err != 0) {
		return err;
	}
	err = read_register(CHARGER_BASE, CHARGER_STATUS, &charge_status);
	if (err != 0) {
		return err;
	}
	err = read_register(CHARGER_BASE, CHARGER_ERROR, &charge_error);
	if (err != 0) {
		return err;
	}
	err = read_register(CHARGER_BASE, CHARGER_CURRENT_MSB, &current_msb);
	if (err != 0) {
		return err;
	}
	err = read_register(CHARGER_BASE, CHARGER_CURRENT_LSB, &current_lsb);
	if (err != 0) {
		return err;
	}
	err = read_register(CHARGER_BASE, CHARGER_TERMINATION_VOLTAGE, &termination);
	if (err != 0) {
		return err;
	}
	err = read_register(ADC_BASE, ADC_CONFIGURATION, &adc_configuration);
	if (err != 0) {
		return err;
	}
	err = write_register(ADC_BASE, ADC_MEASURE_VBAT, 1);
	if (err != 0) {
		return err;
	}
	err = write_register(ADC_BASE, ADC_MEASURE_VSYS, 1);
	if (err != 0) {
		return err;
	}
	err = write_register(ADC_BASE, ADC_MEASURE_VBUS, 1);
	if (err != 0) {
		return err;
	}
	/* 250 us per conversion; allow VBAT burst, IBAT, VSYS and VBUS to finish. */
	k_msleep(3);
	bool burst = (adc_configuration & ADC_BURST_ENABLED) != 0;

	err = read_register(ADC_BASE, burst ? ADC_BURST_VBAT0_MSB : ADC_VBAT_MSB,
			    &vbat_msb);
	if (err != 0) {
		return err;
	}
	err = read_register(ADC_BASE, burst ? ADC_BURST_RESULT_LSB : ADC_RESULT_LSB,
			    &vbat_lsb);
	if (err != 0) {
		return err;
	}
	err = read_register(ADC_BASE, ADC_VSYS_MSB, &vsys_msb);
	if (err != 0) {
		return err;
	}
	err = read_register(ADC_BASE, ADC_RESULT_LSB, &vsys_lsb);
	if (err != 0) {
		return err;
	}
	err = read_register(ADC_BASE, ADC_VBUS_MSB, &vbus_msb);
	if (err != 0) {
		return err;
	}
	err = read_register(ADC_BASE, ADC_BURST_RESULT_LSB, &vbus_lsb);
	if (err != 0) {
		return err;
	}
	uint32_t vbat_mv = (((uint32_t)vbat_msb << 2) | (vbat_lsb & 0x03)) * 5000 / 1024;
	uint32_t vsys_mv = (((uint32_t)vsys_msb << 2) | ((vsys_lsb >> 6) & 0x03)) *
			   6375 / 1024;
	uint32_t vbus_mv = (((uint32_t)vbus_msb << 2) | ((vbus_lsb >> 6) & 0x03)) *
			   7500 / 1024;
	uint32_t current_ma = (uint32_t)current_msb * 4 + (current_lsb & 1) * 2;
	uint32_t termination_mv = termination <= 3 ? 3500 + 50 * termination :
				 termination <= 13 ? 4000 + 50 * (termination - 4) : 0;

	/* BCHGCHARGESTATUS bit 0 is reserved; it cannot detect battery presence. */
	printk("PMIC: USB=%s VBUS=0x%02x VBAT_mV=%u charger=%s status=0x%02x error=0x%02x\n",
	       (usb & VBUS_PRESENT) ? "present" : "absent", usb, vbat_mv,
	       (enable & CHARGER_ENABLED) ? "enabled" : "disabled", charge_status,
	       charge_error);
	printk("PMIC: existing charge I_mA=%u VTERM_mV=%u NTC=%s\n", current_ma,
	       termination_mv, (disable & CHARGER_NTC_DISABLED) ? "disabled" : "enabled");
	printk("PMIC: ADC VSYS_mV=%u VBUS_mV=%u; suspend=0x%02x CC=0x%02x SHPHLD=%u\n",
	       vsys_mv, vbus_mv, suspend, cc_detection, ship_pin & 1);
	return log_buck_status();
}

int pmic_log_status(void)
{
	if (!i2c_is_ready_dt(&pmic)) {
		return -ENODEV;
	}
	return print_power_status();
}

int pmic_init(void)
{
	if (!i2c_is_ready_dt(&pmic)) {
		return -ENODEV;
	}
	int err = configure_charging_led();

	if (err == 0) {
		err = configure_sensor_supply_auto();
	}
	if (err == 0) {
		err = power_on_display();
	}
	if (err == 0) {
		err = print_power_status();
	}
	return err;
}

int pmic_charger_enable(uint16_t termination_mv, uint16_t current_ma)
{
	uint8_t termination;
	uint8_t original_disable;
	uint8_t disable;
	uint8_t enable;
	uint8_t current_msb;
	uint8_t current_lsb;
	uint8_t normal_termination;
	uint8_t warm_termination;
	uint8_t ntc_selection;
	uint8_t ibat_measurement;
	uint8_t expected_ibat_measurement;
	int err;

	if (termination_mv >= 3500 && termination_mv <= 3650 &&
	    (termination_mv - 3500) % 50 == 0) {
		termination = (termination_mv - 3500) / 50;
	} else if (termination_mv >= 4000 && termination_mv <= 4450 &&
		   (termination_mv - 4000) % 50 == 0) {
		termination = 4 + (termination_mv - 4000) / 50;
	} else {
		return -EINVAL;
	}
	if (current_ma < 32 || current_ma > 800 || current_ma % 2 != 0) {
		return -EINVAL;
	}
	if (!i2c_is_ready_dt(&pmic)) {
		return -ENODEV;
	}
	err = read_register(CHARGER_BASE, CHARGER_DISABLE, &original_disable);
	if (err != 0) {
		return err;
	}
	/* The current registers may only be changed while charging is disabled. */
	err = write_register(CHARGER_BASE, CHARGER_ENABLE_CLEAR, CHARGER_ENABLED);
	if (err != 0) {
		return err;
	}
	err = read_register(CHARGER_BASE, CHARGER_ENABLE, &enable);
	if (err != 0) {
		return err;
	}
	if ((enable & CHARGER_ENABLED) != 0) {
		return -EIO;
	}
	err = write_register(CHARGER_BASE, CHARGER_CURRENT_MSB, current_ma / 4);
	if (err != 0) {
		return err;
	}
	err = write_register(CHARGER_BASE, CHARGER_CURRENT_LSB, (current_ma / 2) & 1);
	if (err != 0) {
		return err;
	}
	err = write_register(CHARGER_BASE, CHARGER_TERMINATION_VOLTAGE, termination);
	if (err != 0) {
		return err;
	}
	/* As in the SDK binding, omitted warm voltage uses the normal voltage.
	 * Battery temperature regions are unused on this board without an NTC.
	 */
	err = write_register(CHARGER_BASE, CHARGER_WARM_TERMINATION_VOLTAGE, termination);
	if (err != 0) {
		return err;
	}
	/* v2 grounds NTC. W1S bit 1 disables only battery NTC monitoring;
	 * automatic recharge and internal die temperature protection are preserved.
	 */
	err = write_register(CHARGER_BASE, CHARGER_DISABLE, CHARGER_NTC_DISABLED);
	if (err != 0) {
		return err;
	}
	err = write_register(ADC_BASE, ADC_NTC_RESISTOR_SELECTION, 0);
	if (err != 0) {
		return err;
	}
	/* Like the SDK charger driver, measure battery current after VBAT. */
	err = read_register(ADC_BASE, ADC_CURRENT_MEASUREMENT_ENABLE, &ibat_measurement);
	if (err != 0) {
		return err;
	}
	expected_ibat_measurement = ibat_measurement | ADC_CURRENT_MEASUREMENT_ENABLED;
	err = write_register(ADC_BASE, ADC_CURRENT_MEASUREMENT_ENABLE,
			     expected_ibat_measurement);
	if (err != 0) {
		return err;
	}
	/* Activate the existing automatic die-temperature measurement interval. */
	err = write_register(ADC_BASE, ADC_UPDATE_AUTO_MEASUREMENT, 1);
	if (err != 0) {
		return err;
	}

	/* Verify configuration before enabling charging. */
	err = read_register(CHARGER_BASE, CHARGER_CURRENT_MSB, &current_msb);
	if (err != 0) {
		return err;
	}
	err = read_register(CHARGER_BASE, CHARGER_CURRENT_LSB, &current_lsb);
	if (err != 0) {
		return err;
	}
	err = read_register(CHARGER_BASE, CHARGER_TERMINATION_VOLTAGE, &normal_termination);
	if (err != 0) {
		return err;
	}
	err = read_register(CHARGER_BASE, CHARGER_WARM_TERMINATION_VOLTAGE,
			    &warm_termination);
	if (err != 0) {
		return err;
	}
	err = read_register(CHARGER_BASE, CHARGER_DISABLE, &disable);
	if (err != 0) {
		return err;
	}
	err = read_register(ADC_BASE, ADC_NTC_RESISTOR_SELECTION, &ntc_selection);
	if (err != 0) {
		return err;
	}
	err = read_register(ADC_BASE, ADC_CURRENT_MEASUREMENT_ENABLE, &ibat_measurement);
	if (err != 0) {
		return err;
	}
	if (current_msb != current_ma / 4 || (current_lsb & 1) != ((current_ma / 2) & 1) ||
	    normal_termination != termination || warm_termination != termination ||
	    disable != (original_disable | CHARGER_NTC_DISABLED) || ntc_selection != 0 ||
	    ibat_measurement != expected_ibat_measurement) {
		printk("PMIC: charger configuration readback mismatch\n");
		return -EIO;
	}
	err = write_register(CHARGER_BASE, CHARGER_CLEAR_ERROR_REGISTERS, 1);
	if (err != 0) {
		return err;
	}
	/* TASKRELEASEERROR matches the SDK charger's enable operation. */
	err = write_register(CHARGER_BASE, CHARGER_RELEASE_ERROR, 1);
	if (err != 0) {
		return err;
	}
	err = write_register(CHARGER_BASE, CHARGER_ENABLE, CHARGER_ENABLED);
	if (err != 0) {
		return err;
	}
	err = read_register(CHARGER_BASE, CHARGER_ENABLE, &enable);
	if (err != 0) {
		return err;
	}
	if ((enable & CHARGER_ENABLED) == 0) {
		printk("PMIC: charger enable readback failed\n");
		return -EIO;
	}
	printk("PMIC: charging enabled VTERM_mV=%u I_mA=%u; LED1 follows charging\n",
	       termination_mv, current_ma);
	return print_power_status();
}
