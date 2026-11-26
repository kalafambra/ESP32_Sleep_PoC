#pragma once

#include <initializer_list>

#include "Button.h"
#include "TiltDetector.h"

class DeviceController {
 public:
  enum class State { Disarmed, Arming, Armed, Fault };
  enum class Event { None, ArmingStarted, ReferenceWindowStarted, ReferenceWindowElapsed, Disarmed, ArmRefused };

  struct IdleState {
    UptimeMs deadlineMs;
    bool bootWakeEnabled;
    bool motionWakeEnabled;
    bool bootPressed;
  };

  void initializeButton(UptimeMs nowMs, bool pressed) { button_ = Button(pressed, nowMs); }

  // I/O runs synchronously on the caller's task. Each pass services at most one
  // tilt observation and one BME conversion step, even after a late start.
  template <typename Io>
  IdleState run(Io &io) {
    UptimeMs buttonCheckedMs = serviceInputs(io);
    const UptimeMs accelMs = io.now();
    if (state_ == State::Arming && reference_.due(accelMs)) {
      io.sampleReference(accelMs);
    }
    const bool tiltDue = state_ == State::Armed && accelMs >= tilt_.nextDeadline();
    if (tiltDue) {
      io.checkTilt(accelMs);
      buttonCheckedMs = serviceInputs(io);
    }

    const UptimeMs envMs = io.now();
    bool environmentWork = false;
    if (state_ == State::Armed && nextEnvironmentMs_ != NO_DEADLINE_MS) {
      if (envMs >= nextEnvironmentMs_) {
        const UptimeMs slots = 1 + (envMs - nextEnvironmentMs_) / Config::ENV_INTERVAL_MS;
        nextEnvironmentMs_ += slots * Config::ENV_INTERVAL_MS;
        if (slots != 1 || !io.startEnvironment(envMs)) {
          missedEnvironmentSlots_ += slots;
          io.environmentMissed(envMs, slots, missedEnvironmentSlots_);
        }
        environmentWork = true;
      }
      const UptimeMs conversionMs = io.now();
      if (conversionMs >= io.environmentDeadline()) {
        io.advanceEnvironment(conversionMs);
        environmentWork = true;
      }
    }
    if (environmentWork) {
      // GPIO or BOOT qualification can become ready during I2C. A qualified
      // disarm cancels pending work before the result can be published.
      buttonCheckedMs = serviceInputs(io);
    }
    if ((tiltDue || environmentWork) && state_ == State::Armed) io.report();
    return idleState(io, buttonCheckedMs);
  }

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
          clearSchedule();
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
  constexpr void completeArm(UptimeMs nowMs, const GravityReference &reference,
                             bool tiltEnabled, bool environmentEnabled) {
    state_ = State::Armed;
    deadlineMs_ = NO_DEADLINE_MS;
    if (tiltEnabled) tilt_.arm(nowMs, reference);
    else tilt_.disarm();
    reference_ = ReferenceCollector{};
    nextEnvironmentMs_ = environmentEnabled ? nowMs + Config::ENV_INTERVAL_MS : NO_DEADLINE_MS;
    missedEnvironmentSlots_ = 0;
  }

  constexpr void enterFault() {
    state_ = State::Fault;
    clearSchedule();
  }

  constexpr State state() const { return state_; }
  constexpr UptimeMs nextDeadline() const { return deadlineMs_; }
  ReferenceCollector &reference() { return reference_; }
  TiltDetector &tilt() { return tilt_; }

 private:
  constexpr void clearSchedule() {
    deadlineMs_ = NO_DEADLINE_MS;
    nextEnvironmentMs_ = NO_DEADLINE_MS;
    reference_ = ReferenceCollector{};
    tilt_.disarm();
  }

  template <typename Io>
  UptimeMs serviceInputs(Io &io) {
    const bool armed = state_ == State::Armed;
    const UptimeMs motionMs = io.now();
    const UptimeMs retryMs = io.motionRetryAt();
    if (armed && io.motionEnabled() && motionMs >= retryMs && (retryMs != 0 || io.motionAsserted())) {
      io.serviceMotion(motionMs);
    }
    const UptimeMs nowMs = io.now();
    const bool wasArmed = state_ == State::Armed;
    const bool press = button_.tick(nowMs, io.bootPressed());
    const Event event = tick(nowMs, press, io.readyToArm());
    if (event != Event::None) io.lifecycleEvent(event, nowMs, wasArmed);
    return nowMs;
  }

  template <typename Io>
  IdleState idleState(Io &io, UptimeMs buttonCheckedMs) const {
    const bool motionEnabled = state_ == State::Armed && io.motionEnabled();
    const bool bootPressed = io.bootPressed();
    const bool bootChanged = button_.inputChanged(bootPressed);
    const bool motionAsserted = motionEnabled && io.motionAsserted();
    const UptimeMs nowMs = io.now();
    const UptimeMs retryMs = io.motionRetryAt();
    const bool motionReady = motionEnabled && nowMs >= retryMs;
    UptimeMs deadlineMs = button_.nextDeadline(buttonCheckedMs);
    if (bootChanged || (motionReady && motionAsserted)) deadlineMs = nowMs;
    for (const UptimeMs deadline : {deadlineMs_, reference_.nextDeadline(), tilt_.nextDeadline(),
                                  nextEnvironmentMs_, io.environmentDeadline()}) {
      if (deadline < deadlineMs) deadlineMs = deadline;
    }
    if (motionEnabled && retryMs != 0 && retryMs < deadlineMs) deadlineMs = retryMs;
    return {deadlineMs, !button_.wakeMasked(), motionReady, bootPressed};
  }

  Button button_;
  ReferenceCollector reference_;
  TiltDetector tilt_;
  State state_ = State::Disarmed;
  UptimeMs deadlineMs_ = NO_DEADLINE_MS;
  UptimeMs nextEnvironmentMs_ = NO_DEADLINE_MS;
  UptimeMs missedEnvironmentSlots_ = 0;
  bool referenceStarted_ = false;
};
