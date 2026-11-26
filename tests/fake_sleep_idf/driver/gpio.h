#pragma once

#include <esp_err.h>

using gpio_num_t = int;
enum gpio_int_type_t { GPIO_INTR_LOW_LEVEL, GPIO_INTR_HIGH_LEVEL };

esp_err_t gpio_wakeup_enable(gpio_num_t pin, gpio_int_type_t polarity);
esp_err_t gpio_wakeup_disable(gpio_num_t pin);
int gpio_get_level(gpio_num_t pin);
