#pragma once

#include <esp_sleep.h>

#include "SchedulerDeadlines.h"

class SleepTrace {
 public:
  using WriteLine = void (*)(const char *line);
  explicit constexpr SleepTrace(WriteLine write) : write_(write) {}

  bool announce(uint64_t nowUs, UptimeMs deadlineMs);
  void abort(uint64_t nowUs, const char *reason, esp_err_t error = ESP_OK);
  void woke(uint64_t nowUs, uint64_t elapsedUs, esp_sleep_wakeup_cause_t cause, bool bootObserved,
            bool motionObserved, bool bootChanged, UptimeMs deadlineMs, const SchedulerDeadlines &deadlines);
  bool pending() const { return pending_; }

 private:
  WriteLine write_;
  uint64_t lastWakeUs_ = 0;
  bool pending_ = false;
};
