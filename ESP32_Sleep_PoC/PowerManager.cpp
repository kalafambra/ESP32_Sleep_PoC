#include "PowerManager.h"

#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_sleep.h>
#include <esp_timer.h>

#include "config.h"

namespace {

constexpr gpio_num_t bootPin = static_cast<gpio_num_t>(Config::BOOT_PIN);
constexpr gpio_num_t motionPin = static_cast<gpio_num_t>(Config::MPU_INT_PIN);

esp_err_t setPinWake(gpio_num_t pin, gpio_int_type_t polarity, bool enabled, bool &applied) {
  if (enabled == applied) return ESP_OK;
  const esp_err_t result = enabled ? gpio_wakeup_enable(pin, polarity) : gpio_wakeup_disable(pin);
  if (result == ESP_OK) applied = enabled;
  return result;
}

}  // namespace

PowerManager::Outcome PowerManager::sleepUntil(UptimeMs deadlineMs, bool bootWake, bool motionWake,
                                              bool bootPressed) {
  const bool timed = deadlineMs != NO_DEADLINE_MS;
  const uint64_t deadlineUs = timed ? deadlineMs * 1000 : 0;
  const uint64_t nowUs = esp_timer_get_time();
  if (timed && deadlineUs <= nowUs + Config::SLEEP_MIN_WINDOW_US) {
    ++stats_.skips;
    return {deadlineUs <= nowUs ? Action::Reschedule : Action::WaitAwake};
  }

  // The scheduler has completed its synchronous bus work. Let it run again
  // while TX drains if bytes are still on the wire.
  esp_err_t result = uart_wait_tx_done(UART_NUM_0, 0);
  if (result == ESP_ERR_TIMEOUT) {
    ++stats_.skips;
    return {Action::WaitAwake};
  }
  if (result != ESP_OK) return {Action::Fault, "UART0 TX check", result};

  result = setPinWake(bootPin, GPIO_INTR_LOW_LEVEL, bootWake, bootWake_);
  if (result != ESP_OK) return {Action::Fault, "BOOT wake configuration", result};
  result = setPinWake(motionPin, GPIO_INTR_HIGH_LEVEL, motionWake, motionWake_);
  if (result != ESP_OK) return {Action::Fault, "MPU wake configuration", result};
  const bool gpioWake = bootWake || motionWake;
  if (gpioWake != gpioWake_) {
    result = gpioWake ? esp_sleep_enable_gpio_wakeup() : esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
    if (result != ESP_OK) return {Action::Fault, "GPIO wake configuration", result};
    gpioWake_ = gpioWake;
  }
  if (!timed && timerWake_) {
    result = esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    if (result != ESP_OK) return {Action::Fault, "timer wake disable", result};
    timerWake_ = false;
  }
  if (!timed && !gpioWake) return {Action::Fault, "no sleep wake source", ESP_ERR_INVALID_STATE};

  const auto inputsPending = [=]() {
    const bool pressed = gpio_get_level(bootPin) == 0;
    return (pressed != bootPressed) || (bootWake && pressed) ||
           (motionWake && gpio_get_level(motionPin) != 0);
  };
  if (inputsPending()) {
    ++stats_.skips;
    return {Action::Reschedule};
  }

  const uint64_t timerStartUs = esp_timer_get_time();
  if (timed) {
    if (deadlineUs <= timerStartUs + Config::SLEEP_MIN_WINDOW_US) {
      ++stats_.skips;
      return {deadlineUs <= timerStartUs ? Action::Reschedule : Action::WaitAwake};
    }
    result = esp_sleep_enable_timer_wakeup(deadlineUs - timerStartUs);
    if (result != ESP_OK) return {Action::Fault, "timer wake configuration", result};
    timerWake_ = true;
  }

  // A GPIO asserted after the last check is still a hardware wake source.
  const uint64_t beforeUs = esp_timer_get_time();
  result = esp_light_sleep_start();
  const uint64_t elapsedUs = esp_timer_get_time() - beforeUs;
  if (result != ESP_OK) {
    ++stats_.entryErrors;
    if (result == ESP_ERR_SLEEP_REJECT && inputsPending()) {
      consecutiveEntryErrors_ = 0;
      return {Action::Reschedule};
    }
    if (++consecutiveEntryErrors_ >= Config::SLEEP_FAILURE_LIMIT) {
      return {Action::Fault, "light sleep entry", result};
    }
    return {Action::WaitAwake};
  }

  consecutiveEntryErrors_ = 0;
  stats_.sleepCallUs += elapsedUs;
  switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_TIMER: ++stats_.timerWakes; break;
    case ESP_SLEEP_WAKEUP_GPIO:
      ++stats_.gpioWakes;
      // The original ESP32 provides a GPIO wake category, so sample the
      // enabled pins before the scheduler acknowledges or qualifies them.
      if (bootWake && gpio_get_level(bootPin) == 0) ++stats_.bootObserved;
      if (motionWake && gpio_get_level(motionPin) != 0) ++stats_.motionObserved;
      break;
    default: ++stats_.otherWakes; break;
  }
  return {Action::Reschedule};
}
