#ifndef CARD_CO2_ALARM_H_
#define CARD_CO2_ALARM_H_

#include <stdbool.h>
#include <stdint.h>

/** Initialize the hardware PWM and leave the buzzer silent. */
int co2_alarm_init(void);

/** Asynchronously sound five short beeps on a valid >1200 ppm transition.
 * A valid <=1200 ppm sample rearms; invalid samples never rearm the alarm.
 */
void co2_alarm_evaluate(bool valid, int32_t co2_ppm);

/** Cancel any remaining beeps, stop PWM, and drive P1.01 low. */
int co2_alarm_stop(void);

#endif
