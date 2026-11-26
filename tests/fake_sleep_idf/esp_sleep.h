#pragma once

#include <stdint.h>
#include <esp_err.h>

enum esp_sleep_wakeup_cause_t { ESP_SLEEP_WAKEUP_TIMER, ESP_SLEEP_WAKEUP_GPIO, ESP_SLEEP_WAKEUP_UNDEFINED };
constexpr esp_err_t ESP_ERR_SLEEP_REJECT = ESP_ERR_INVALID_STATE;

esp_err_t esp_sleep_enable_gpio_wakeup();
esp_err_t esp_sleep_enable_timer_wakeup(uint64_t durationUs);
esp_err_t esp_sleep_disable_wakeup_source(esp_sleep_wakeup_cause_t source);
esp_err_t esp_light_sleep_start();
esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause();
