#ifndef OUTDOOR_BUZZER_H_
#define OUTDOOR_BUZZER_H_

/** Initialize the board's buzzer and leave it silent. Returns 0 or a negative errno. */
int buzzer_init(void);

/** Start PWM at the configured frequency and duty. Requires successful initialization. */
int buzzer_start(void);

/** Stop PWM and drive the output low. Requires successful initialization. */
int buzzer_stop(void);

#endif /* OUTDOOR_BUZZER_H_ */
