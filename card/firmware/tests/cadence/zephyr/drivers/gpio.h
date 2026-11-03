#ifndef CARD_CADENCE_TEST_GPIO_H_
#define CARD_CADENCE_TEST_GPIO_H_
struct gpio_dt_spec { int unused; };
#define DT_PATH(...) 0
#define GPIO_DT_SPEC_GET(...) { 0 }
static int mock_push_input;
static unsigned int mock_gpio_reads;
static inline int gpio_pin_get_dt(const struct gpio_dt_spec *spec)
{
	(void)spec;
	++mock_gpio_reads;
	return mock_push_input;
}
#endif
