#ifndef CARD_PMIC_H
#define CARD_PMIC_H

#include <stdint.h>

/* Call after +V powers the I2C pull-ups, before starting sensor sampling.
 * Selects BUCK2 Auto mode while preserving its voltage, and configures display BUCK1
 * and the automatic charging LED. Charger current, termination voltage and
 * enable state are preserved.
 */
int pmic_init(void);

/* Valid voltage: 3500..3650 or 4000..4450 mV, in 50 mV steps.
 * Valid current: 32..800 mA, in 2 mA steps. The v2 NTC pin is grounded.
 */
int pmic_charger_enable(uint16_t termination_mv, uint16_t current_ma);

/* Trigger voltage measurements and log PMIC supply, charger and BUCK state.
 * Call while sensor I2C traffic is paused.
 */
int pmic_log_status(void);

#endif
