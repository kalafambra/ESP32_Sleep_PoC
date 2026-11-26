#pragma once

#include <esp_err.h>

#include "SchedulerDeadlines.h"

class SleepTrace;

class PowerManager {
 public:
  explicit constexpr PowerManager(SleepTrace *trace = nullptr) : trace_(trace) {}

  enum class Action { Reschedule, WaitAwake, Fault };

  struct Outcome {
    Action action;
    const char *operation = nullptr;
    esp_err_t error = ESP_OK;
  };

  struct Stats {
    uint64_t timerWakes = 0;
    uint64_t gpioWakes = 0;
    uint64_t otherWakes = 0;
    uint64_t bootObserved = 0;
    uint64_t motionObserved = 0;
    uint64_t entryErrors = 0;
    uint64_t skips = 0;
    uint64_t sleepCallUs = 0;
  };

  Outcome sleepUntil(UptimeMs deadlineMs, bool bootWake, bool motionWake, bool bootPressed,
                     const SchedulerDeadlines &deadlines = {});
  void resetStats() { stats_ = {}; }
  const Stats &stats() const { return stats_; }

 private:
  Stats stats_;
  SleepTrace *trace_;
  bool bootWake_ = false;
  bool motionWake_ = false;
  bool gpioWake_ = false;
  bool timerWake_ = false;
  uint8_t consecutiveEntryErrors_ = 0;
};
