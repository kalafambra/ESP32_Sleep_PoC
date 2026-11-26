#pragma once

#include "Types.h"
#include "config.h"

class DeviceController {
 public:
  enum class State { Disarmed, Arming, Armed, Fault };
  enum class Event { None, ArmingStarted, ReferenceWindowStarted, ReferenceWindowElapsed, Disarmed, ArmRefused };

  constexpr Event tick(UptimeMs nowMs, bool press, bool readyToArm) {
    if (press) {
      switch (state_) {
        case State::Disarmed:
          if (!readyToArm) {
            enterFault();
            return Event::ArmRefused;
          }
          state_ = State::Arming;
          referenceStarted_ = false;
          deadlineMs_ = nowMs + Config::ARMING_DELAY_MS - Config::ZEROING_TIME_MS;
          return Event::ArmingStarted;
        case State::Arming:
        case State::Armed:
          state_ = State::Disarmed;
          deadlineMs_ = NO_DEADLINE_MS;
          return Event::Disarmed;
        case State::Fault:
          return Event::None;
      }
    }
    if (state_ != State::Arming || nowMs < deadlineMs_) return Event::None;
    if (!referenceStarted_) {
      referenceStarted_ = true;
      deadlineMs_ = nowMs + Config::ZEROING_TIME_MS;
      return Event::ReferenceWindowStarted;
    }
    return Event::ReferenceWindowElapsed;
  }

  // Start another full reference window while remaining in ARMING.
  constexpr void retryReference(UptimeMs nowMs) {
    deadlineMs_ = nowMs + Config::ZEROING_TIME_MS;
  }

  // The caller completes the pending arm after reference validation and hardware checks.
  constexpr void completeArm() {
    state_ = State::Armed;
    deadlineMs_ = NO_DEADLINE_MS;
  }

  constexpr void enterFault() {
    state_ = State::Fault;
    deadlineMs_ = NO_DEADLINE_MS;
  }

  constexpr State state() const { return state_; }
  constexpr UptimeMs nextDeadline() const { return deadlineMs_; }

 private:
  State state_ = State::Disarmed;
  UptimeMs deadlineMs_ = NO_DEADLINE_MS;
  bool referenceStarted_ = false;
};
