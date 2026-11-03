#include "mock.h"
struct gpio_dt_spec {int unused;};
#define DT_PATH(...) 0
#define GPIO_DT_SPEC_GET(...) {0}
static int gpio_pin_get_dt(const struct gpio_dt_spec *spec){(void)spec;return 0;}
