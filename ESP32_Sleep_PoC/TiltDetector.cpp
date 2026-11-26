#include "TiltDetector.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr uint32_t expectedReferenceSamples = Config::ZEROING_TIME_MS / Config::ZEROING_SAMPLE_MS;
constexpr uint32_t minReferenceSamples =
    (expectedReferenceSamples * Config::ZEROING_MIN_SAMPLE_PERCENT + 99) / 100;
constexpr double radToDeg = 180.0 / 3.14159265358979323846;
// Allow rounding of the float vector components at inclusive angle boundaries.
constexpr double angleToleranceDeg = 1e-5;

double squaredMagnitude(const Vec3 &vector) {
  return static_cast<double>(vector.x) * vector.x +
         static_cast<double>(vector.y) * vector.y +
         static_cast<double>(vector.z) * vector.z;
}

}  // namespace

void ReferenceCollector::start(UptimeMs nowMs) {
  *this = ReferenceCollector{};
  nextSampleMs_ = nowMs;
  endMs_ = nowMs + Config::ZEROING_TIME_MS;
}

void ReferenceCollector::observe(UptimeMs nowMs, const Result<Vec3> &sample) {
  if (!due(nowMs)) return;
  nextSampleMs_ += ((nowMs - nextSampleMs_) / Config::ZEROING_SAMPLE_MS + 1) * Config::ZEROING_SAMPLE_MS;
  const double squared = squaredMagnitude(sample.value);
  if (!sample.valid() || !std::isfinite(squared)) {
    invalid_ = true;
    return;
  }
  ++count_;
  sumX_ += sample.value.x;
  sumY_ += sample.value.y;
  sumZ_ += sample.value.z;
  sumSquares_ += squared;
}

ReferenceCollector::Outcome ReferenceCollector::finish() const {
  Outcome outcome{};
  outcome.samples = count_;
  if (invalid_) {
    outcome.failure = Failure::InvalidSample;
    return outcome;
  }
  if (count_ < minReferenceSamples) return outcome;

  const double x = sumX_ / count_;
  const double y = sumY_ / count_;
  const double z = sumZ_ / count_;
  const double squaredMean = x * x + y * y + z * z;
  const double magnitude = std::sqrt(squaredMean);
  outcome.rmsResidualG = std::sqrt(std::max(0.0, sumSquares_ / count_ - squaredMean));
  if (magnitude < Config::ZEROING_MIN_MAGNITUDE_G || magnitude > Config::ZEROING_MAX_MAGNITUDE_G) {
    outcome.failure = Failure::Magnitude;
  } else if (outcome.rmsResidualG > Config::ZEROING_MAX_RMS_RESIDUAL_G) {
    outcome.failure = Failure::Movement;
  } else {
    outcome.failure = Failure::None;
    outcome.reference = {{static_cast<float>(x / magnitude), static_cast<float>(y / magnitude),
                           static_cast<float>(z / magnitude)}, static_cast<float>(magnitude)};
  }
  return outcome;
}

void TiltDetector::arm(UptimeMs nowMs, const GravityReference &reference) {
  disarm();
  reference_ = reference;
  nextCheckMs_ = nowMs + Config::TILT_INTERVAL_MS;
  filterCount_ = Config::TILT_FILTER_SAMPLES;
  latest_.angleValid = true;
}

void TiltDetector::breakContinuity() {
  candidateSinceMs_ = NO_DEADLINE_MS;
  filterCount_ = 0;
  filterIndex_ = 0;
  latest_.angleValid = false;
}

TiltDetector::Observation TiltDetector::observe(UptimeMs nowMs, const Result<Vec3> &sample) {
  latest_.transition = Transition::None;
  if (nowMs < nextCheckMs_) return latest_;

  const UptimeMs skipped = (nowMs - nextCheckMs_) / Config::TILT_INTERVAL_MS;
  nextCheckMs_ += (skipped + 1) * Config::TILT_INTERVAL_MS;
  if (skipped != 0) {
    missedChecks_ += skipped;
    breakContinuity();
  }

  const auto &a = sample.value;
  const double magnitude = std::sqrt(squaredMagnitude(a));
  if (!sample.valid() || !std::isfinite(magnitude) || magnitude == 0.0 ||
      magnitude < static_cast<double>(reference_.magnitudeG) * Config::TILT_MIN_VALID_RATIO ||
      magnitude > static_cast<double>(reference_.magnitudeG) * Config::TILT_MAX_VALID_RATIO) {
    breakContinuity();
    return latest_;
  }

  const auto &ref = reference_.unit;
  const double dot = (static_cast<double>(ref.x) * a.x + static_cast<double>(ref.y) * a.y +
                      static_cast<double>(ref.z) * a.z) /
                     (std::sqrt(squaredMagnitude(ref)) * magnitude);
  anglesDeg_[filterIndex_] = std::acos(std::max(-1.0, std::min(1.0, dot))) * radToDeg;
  filterIndex_ = (filterIndex_ + 1) % Config::TILT_FILTER_SAMPLES;
  if (filterCount_ < Config::TILT_FILTER_SAMPLES) ++filterCount_;
  if (filterCount_ < Config::TILT_FILTER_SAMPLES) return latest_;

  double sorted[Config::TILT_FILTER_SAMPLES];
  std::copy_n(anglesDeg_, Config::TILT_FILTER_SAMPLES, sorted);
  std::sort(sorted, sorted + Config::TILT_FILTER_SAMPLES);
  latest_.angleDeg = sorted[Config::TILT_FILTER_SAMPLES / 2];
  latest_.angleValid = true;
  const double clearThreshold = Config::TILT_THRESHOLD_DEG - Config::TILT_HYSTERESIS_DEG;
  const bool qualifies = tilted_ ? latest_.angleDeg <= clearThreshold + angleToleranceDeg
                                 : latest_.angleDeg >= Config::TILT_THRESHOLD_DEG - angleToleranceDeg;
  if (!qualifies) {
    candidateSinceMs_ = NO_DEADLINE_MS;
  } else if (candidateSinceMs_ == NO_DEADLINE_MS) {
    candidateSinceMs_ = nowMs;
  } else if (nowMs - candidateSinceMs_ >= Config::TILT_PERSISTENCE_MS) {
    tilted_ = !tilted_;
    candidateSinceMs_ = NO_DEADLINE_MS;
    latest_.transition = tilted_ ? Transition::Detected : Transition::Cleared;
  }
  return latest_;
}
