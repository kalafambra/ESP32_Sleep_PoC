#pragma once

#include "Types.h"
#include "config.h"

struct GravityReference {
  Vec3 unit{};
  float magnitudeG = 0.0f;
};

class ReferenceCollector {
 public:
  enum class Failure { None, InvalidSample, TooFewSamples, Magnitude, Movement };
  struct Outcome {
    GravityReference reference{};
    Failure failure = Failure::TooFewSamples;
    uint32_t samples = 0;
    double rmsResidualG = 0.0;
  };

  void start(UptimeMs nowMs);
  bool due(UptimeMs nowMs) const { return nowMs >= nextSampleMs_ && nowMs < endMs_; }
  UptimeMs nextDeadline() const { return nextSampleMs_ < endMs_ ? nextSampleMs_ : NO_DEADLINE_MS; }
  void observe(UptimeMs nowMs, const Result<Vec3> &sample);
  Outcome finish() const;

 private:
  UptimeMs nextSampleMs_ = NO_DEADLINE_MS;
  UptimeMs endMs_ = 0;
  uint32_t count_ = 0;
  bool invalid_ = false;
  double sumX_ = 0.0;
  double sumY_ = 0.0;
  double sumZ_ = 0.0;
  double sumSquares_ = 0.0;
};

class TiltDetector {
 public:
  enum class Transition { None, Detected, Cleared };
  struct Observation {
    Transition transition = Transition::None;
    double angleDeg = 0.0;
    bool angleValid = false;
  };

  void arm(UptimeMs nowMs, const GravityReference &reference);
  constexpr void disarm() { *this = TiltDetector{}; }
  UptimeMs nextDeadline() const { return nextCheckMs_; }
  Observation observe(UptimeMs nowMs, const Result<Vec3> &sample);
  bool tilted() const { return tilted_; }
  uint64_t missedChecks() const { return missedChecks_; }

 private:
  void breakContinuity();

  GravityReference reference_{};
  UptimeMs nextCheckMs_ = NO_DEADLINE_MS;
  UptimeMs candidateSinceMs_ = NO_DEADLINE_MS;
  uint64_t missedChecks_ = 0;
  double anglesDeg_[Config::TILT_FILTER_SAMPLES]{};
  uint8_t filterCount_ = 0;
  uint8_t filterIndex_ = 0;
  bool tilted_ = false;
  Observation latest_{};
};
