#include "SleepTrace.h"

#include <string.h>

#include "Uptime.h"
#include "config.h"

namespace {

constexpr const char *workNames[] = {
    "BOOT handling", "arming countdown", "zeroing completion", "zeroing sample",
    "MPU6050 tilt check", "BME280 burst start", "BME280 conversion check", "MPU6050 motion service",
};
static_assert(sizeof(workNames) / sizeof(*workNames) == SchedulerDeadlines::Count, "Trace work labels");

void appendWork(char (&work)[192], const char *reason) {
  const size_t used = strlen(work);
  snprintf(work + used, sizeof(work) - used, "%s%s", used ? ", " : "", reason);
}

}  // namespace

bool SleepTrace::announce(uint64_t nowUs, UptimeMs deadlineMs) {
  char timestamp[UPTIME_TEXT_SIZE];
  formatUptime(nowUs / 1000, timestamp);
  const uint64_t awakeUs = nowUs - lastWakeUs_;
  char line[96];
  snprintf(line, sizeof(line), "[%s] [DEBUG] SLEEP_ENTERING mode=light-sleep awake_ms=%" PRIu64 ".%03" PRIu64 "\n",
           timestamp, awakeUs / 1000, awakeUs % 1000);
  // UART is idle. Budget the full 8N1 line and a cooperative retry before the entry guard.
  constexpr uint64_t usPerByte = 10 * 1000000ULL;
  const uint64_t drainUs = (strlen(line) * usPerByte + Config::SERIAL_BAUD - 1) / Config::SERIAL_BAUD;
  if (deadlineMs != NO_DEADLINE_MS &&
      deadlineMs * 1000 <= nowUs + drainUs + Config::SLEEP_RETRY_MS * 1000ULL + Config::SLEEP_MIN_WINDOW_US) {
    return false;
  }
  pending_ = true;
  write_(line);
  return true;
}

void SleepTrace::abort(uint64_t nowUs, const char *reason, esp_err_t error) {
  if (!pending_) return;
  pending_ = false;
  char timestamp[UPTIME_TEXT_SIZE];
  formatUptime(nowUs / 1000, timestamp);
  char errorText[20] = "";
  if (error != ESP_OK) snprintf(errorText, sizeof(errorText), " error=0x%X", static_cast<unsigned>(error));
  char line[160];
  snprintf(line, sizeof(line), "[%s] [DEBUG] SLEEP_ABORTED mode=light-sleep reason=%s%s\n",
           timestamp, reason, errorText);
  write_(line);
}

void SleepTrace::woke(uint64_t nowUs, uint64_t elapsedUs, esp_sleep_wakeup_cause_t cause, bool bootObserved,
                     bool motionObserved, bool bootChanged, UptimeMs deadlineMs, const SchedulerDeadlines &deadlines) {
  lastWakeUs_ = nowUs;
  pending_ = false;
  const UptimeMs nowMs = nowUs / 1000;
  const bool timer = cause == ESP_SLEEP_WAKEUP_TIMER;
  const char *observed = "";
  if (cause == ESP_SLEEP_WAKEUP_GPIO) {
    observed = bootObserved ? (motionObserved ? " observed=GPIO0,GPIO4" : " observed=GPIO0")
                            : (motionObserved ? " observed=GPIO4" : " observed=unresolved");
  }
  char work[192] = "";
  char earlyTimerWork[192] = "";
  for (unsigned i = 0; i < SchedulerDeadlines::Count; ++i) {
    const UptimeMs at = deadlines.at[i];
    const bool inputPending = (i == SchedulerDeadlines::Boot && (bootObserved || bootChanged)) ||
                              (i == SchedulerDeadlines::Motion && motionObserved);
    if (inputPending || at <= nowMs) {
      appendWork(work, workNames[i]);
    } else if (timer && at == deadlineMs && at != NO_DEADLINE_MS) {
      appendWork(earlyTimerWork, workNames[i]);
    }
  }
  char causeText[24];
  snprintf(causeText, sizeof(causeText), "other(%d)", static_cast<int>(cause));
  char timestamp[UPTIME_TEXT_SIZE];
  formatUptime(nowMs, timestamp);
  char line[512];
  snprintf(line, sizeof(line),
           "[%s] [DEBUG] WOKE mode=light-sleep cause=%s sleep_call_ms=%" PRIu64 ".%03" PRIu64 "%s work=%s%s%s\n",
           timestamp, timer ? "timer" : (cause == ESP_SLEEP_WAKEUP_GPIO ? "GPIO" : causeText),
           elapsedUs / 1000, elapsedUs % 1000, observed, work[0] ? work : "none",
           earlyTimerWork[0] ? " timer_work=" : "", earlyTimerWork);
  write_(line);
}
