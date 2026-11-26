#include <cassert>
#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <string>
#include <vector>

#include "PowerManager.h"
#include "SleepTrace.h"
#include "config.h"

namespace {

struct Fake {
  int64_t nowUs = (1LL << 32) * 1000 + 123;
  uint64_t timerUs = 0;
  unsigned sleepCalls = 0;
  bool bootPressed = false;
  bool motionAsserted = false;
  bool bootWake = false;
  bool motionWake = false;
  bool gpioWake = false;
  bool assertPinsAtEntry = false;
  esp_err_t txResult = ESP_OK;
  esp_err_t timerResult = ESP_OK;
  esp_err_t sleepResult = ESP_OK;
  esp_sleep_wakeup_cause_t cause = ESP_SLEEP_WAKEUP_TIMER;
  uint64_t sleepElapsedUs = 5000;
} fake;

using Action = PowerManager::Action;

UptimeMs later() {
  return fake.nowUs / 1000 + 20;
}

void timerAndPinWake() {
  fake = {};
  PowerManager power;
  const UptimeMs deadline = later();
  const uint64_t expectedUs = deadline * 1000 - fake.nowUs;
  assert(power.sleepUntil(deadline, true, true, false).action == Action::Reschedule);
  assert(fake.timerUs == expectedUs && fake.bootWake && fake.motionWake && fake.gpioWake);
  assert(power.stats().timerWakes == 1 && power.stats().sleepCallUs == 5000);

  fake.cause = ESP_SLEEP_WAKEUP_GPIO;
  fake.assertPinsAtEntry = true;
  assert(power.sleepUntil(later(), true, true, false).action == Action::Reschedule);
  assert(power.stats().gpioWakes == 1 && power.stats().bootObserved == 1 && power.stats().motionObserved == 1);

  fake.motionAsserted = false;
  assert(power.sleepUntil(later(), false, true, true).action == Action::Reschedule);
  assert(!fake.bootWake && fake.motionWake && fake.gpioWake && fake.sleepCalls == 3);
  assert(power.stats().gpioWakes == 2 && power.stats().bootObserved == 1 && power.stats().motionObserved == 2);

  fake.assertPinsAtEntry = fake.bootPressed = fake.motionAsserted = false;
  assert(power.sleepUntil(NO_DEADLINE_MS, true, false, false).action == Action::Reschedule);
  assert(fake.timerUs == 0 && fake.bootWake && !fake.motionWake);
  power.resetStats();
  assert(power.stats().sleepCallUs == 0 && power.stats().gpioWakes == 0);
}

void guardsAndErrors() {
  fake = {};
  PowerManager power;
  const UptimeMs nowMs = fake.nowUs / 1000;
  assert(power.sleepUntil(nowMs, true, true, false).action == Action::Reschedule);
  assert(power.sleepUntil(nowMs + 1, true, true, false).action == Action::WaitAwake);
  fake.txResult = ESP_ERR_TIMEOUT;
  assert(power.sleepUntil(later(), true, true, false).action == Action::WaitAwake);
  fake.txResult = ESP_OK;
  fake.motionAsserted = true;
  assert(power.sleepUntil(later(), true, true, false).action == Action::Reschedule);
  fake.motionAsserted = false;
  assert(power.sleepUntil(later(), false, true, true).action == Action::Reschedule);
  assert(fake.sleepCalls == 0 && power.stats().skips == 5);

  fake.sleepResult = ESP_ERR_SLEEP_REJECT;
  fake.assertPinsAtEntry = true;
  assert(power.sleepUntil(later(), true, true, false).action == Action::Reschedule);
  assert(power.stats().entryErrors == 1 && power.stats().gpioWakes == 0 && power.stats().sleepCallUs == 0);

  fake.assertPinsAtEntry = fake.bootPressed = fake.motionAsserted = false;
  for (unsigned attempt = 1; attempt < Config::SLEEP_FAILURE_LIMIT; ++attempt) {
    assert(power.sleepUntil(later(), true, true, false).action == Action::WaitAwake);
  }
  const auto fault = power.sleepUntil(later(), true, true, false);
  assert(fault.action == Action::Fault && fault.error == ESP_ERR_SLEEP_REJECT);
  fake.timerResult = ESP_ERR_INVALID_ARG;
  const unsigned calls = fake.sleepCalls;
  assert(power.sleepUntil(later(), true, true, false).action == Action::Fault);
  assert(fake.sleepCalls == calls && power.stats().timerWakes == 0);
}

std::vector<std::string> traceLines;

void recordTrace(const char *line) {
  assert(line[0] == '[' && std::string(line).back() == '\n');
  traceLines.emplace_back(line);
}

void traceWakeReasonsAndTiming() {
  fake = {};
  fake.nowUs = 2212345;
  traceLines.clear();
  SleepTrace trace(recordTrace);
  PowerManager power(&trace);
  const auto deadline = later();
  SchedulerDeadlines work;
  work.at[SchedulerDeadlines::Tilt] = deadline;
  work.at[SchedulerDeadlines::Burst] = deadline;
  work.at[SchedulerDeadlines::Conversion] = deadline + 1;
  assert(power.sleepUntil(deadline, true, true, false, work).action == Action::WaitAwake);
  assert(trace.pending() && fake.sleepCalls == 0 && traceLines.size() == 1);
  assert(traceLines[0] == "[00:00:02.212] [DEBUG] SLEEP_ENTERING mode=light-sleep awake_ms=2212.345\n");

  fake.txResult = ESP_ERR_TIMEOUT;
  for (int i = 0; i < 2; ++i) {
    fake.nowUs += 1000;
    assert(power.sleepUntil(deadline, true, true, false, work).action == Action::WaitAwake);
  }
  assert(trace.pending() && fake.sleepCalls == 0 && traceLines.size() == 1);
  fake.txResult = ESP_OK;
  fake.nowUs += 4000;
  fake.sleepElapsedUs = 15001;
  assert(power.sleepUntil(deadline, true, true, false, work).action == Action::Reschedule);
  assert(!trace.pending() && fake.sleepCalls == 1 && traceLines.size() == 2);
  assert(traceLines[1] == "[00:00:02.233] [DEBUG] WOKE mode=light-sleep cause=timer sleep_call_ms=15.001"
                          " work=MPU6050 tilt check, BME280 burst start, BME280 conversion check\n");
  assert(power.stats().timerWakes == 1 && power.stats().sleepCallUs == 15001);

  power.resetStats();
  fake.nowUs += 10001;
  assert(power.sleepUntil(NO_DEADLINE_MS, true, true, false).action == Action::WaitAwake);
  assert(traceLines[2].find("awake_ms=10.001\n") != std::string::npos);
  fake.nowUs += 7000;
  fake.cause = ESP_SLEEP_WAKEUP_GPIO;
  fake.assertPinsAtEntry = true;
  assert(power.sleepUntil(NO_DEADLINE_MS, true, true, false).action == Action::Reschedule);
  assert(traceLines.size() == 4);
  assert(traceLines[3].find("observed=GPIO0,GPIO4 work=BOOT handling, MPU6050 motion service\n") != std::string::npos);
  assert(power.stats().gpioWakes == 1 && power.stats().motionObserved == 1 && power.stats().bootObserved == 1);

  fake.assertPinsAtEntry = fake.bootPressed = fake.motionAsserted = false;
  assert(power.sleepUntil(NO_DEADLINE_MS, true, false, false).action == Action::WaitAwake);
  assert(power.sleepUntil(NO_DEADLINE_MS, true, false, false).action == Action::Reschedule);
  assert(traceLines.size() == 6 && traceLines[5].find("observed=unresolved work=none\n") != std::string::npos);

  const auto early = later();
  work = {};
  work.at[SchedulerDeadlines::ZeroingSample] = early;
  fake.cause = ESP_SLEEP_WAKEUP_TIMER;
  fake.sleepElapsedUs = 100;
  assert(power.sleepUntil(early, true, false, false, work).action == Action::WaitAwake);
  assert(power.sleepUntil(early, true, false, false, work).action == Action::Reschedule);
  assert(traceLines.size() == 8 && traceLines[7].find("work=none timer_work=zeroing sample\n") != std::string::npos);
}

void traceBudgetCancellationAndRejection() {
  fake = {};
  traceLines.clear();
  SleepTrace trace(recordTrace);
  PowerManager power(&trace);
  const auto tight = fake.nowUs / 1000 + Config::SLEEP_MIN_WINDOW_US / 1000 + 1;
  assert(power.sleepUntil(tight, true, true, false).action == Action::WaitAwake);
  assert(traceLines.empty() && !trace.pending());

  auto deadline = later();
  assert(power.sleepUntil(deadline, true, true, false).action == Action::WaitAwake);
  fake.txResult = ESP_ERR_TIMEOUT;
  fake.motionAsserted = true;
  assert(power.sleepUntil(deadline, true, true, false).action == Action::Reschedule);
  assert(!trace.pending() && traceLines.size() == 2 && fake.sleepCalls == 0);
  assert(traceLines[1].find("SLEEP_ABORTED mode=light-sleep reason=wake input pending\n") != std::string::npos);

  fake.txResult = ESP_OK;
  fake.motionAsserted = false;
  assert(power.sleepUntil(deadline, true, true, false).action == Action::WaitAwake);
  fake.nowUs = deadline * 1000;
  fake.txResult = ESP_ERR_TIMEOUT;
  assert(power.sleepUntil(deadline, true, true, false).action == Action::Reschedule);
  assert(!trace.pending() && traceLines.size() == 4 && fake.sleepCalls == 0);
  assert(traceLines[3].find("reason=scheduler deadline too near\n") != std::string::npos);

  fake.txResult = ESP_OK;
  deadline = later();
  assert(power.sleepUntil(deadline, true, true, false).action == Action::WaitAwake);
  fake.sleepResult = ESP_ERR_SLEEP_REJECT;
  fake.assertPinsAtEntry = true;
  assert(power.sleepUntil(deadline, true, true, false).action == Action::Reschedule);
  assert(!trace.pending() && traceLines.size() == 6 && fake.sleepCalls == 1);
  assert(traceLines[5].find("reason=light sleep rejected error=0x") != std::string::npos);
  assert(power.stats().entryErrors == 1 && power.stats().sleepCallUs == 0);
}

}  // namespace

int64_t esp_timer_get_time() { return fake.nowUs; }
int gpio_get_level(gpio_num_t pin) { return pin == Config::BOOT_PIN ? !fake.bootPressed : fake.motionAsserted; }
esp_err_t gpio_wakeup_enable(gpio_num_t pin, gpio_int_type_t polarity) {
  assert(pin == Config::BOOT_PIN || pin == Config::MPU_INT_PIN);
  if (pin == Config::BOOT_PIN) {
    assert(polarity == GPIO_INTR_LOW_LEVEL);
    fake.bootWake = true;
  } else {
    assert(polarity == GPIO_INTR_HIGH_LEVEL);
    fake.motionWake = true;
  }
  return ESP_OK;
}
esp_err_t gpio_wakeup_disable(gpio_num_t pin) {
  (pin == Config::BOOT_PIN ? fake.bootWake : fake.motionWake) = false;
  return ESP_OK;
}
esp_err_t uart_wait_tx_done(int port, unsigned ticks) {
  assert(port == UART_NUM_0 && ticks == 0);
  return fake.txResult;
}
esp_err_t esp_sleep_enable_gpio_wakeup() {
  fake.gpioWake = true;
  return ESP_OK;
}
esp_err_t esp_sleep_enable_timer_wakeup(uint64_t durationUs) {
  fake.timerUs = durationUs;
  return fake.timerResult;
}
esp_err_t esp_sleep_disable_wakeup_source(esp_sleep_wakeup_cause_t source) {
  if (source == ESP_SLEEP_WAKEUP_TIMER) fake.timerUs = 0;
  else fake.gpioWake = false;
  return ESP_OK;
}
esp_err_t esp_light_sleep_start() {
  ++fake.sleepCalls;
  if (fake.assertPinsAtEntry) fake.bootPressed = fake.motionAsserted = true;
  fake.nowUs += fake.sleepElapsedUs;
  return fake.sleepResult;
}
esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause() { return fake.cause; }

int main() {
  timerAndPinWake();
  guardsAndErrors();
  traceWakeReasonsAndTiming();
  traceBudgetCancellationAndRejection();
}
