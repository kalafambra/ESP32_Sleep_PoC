#include "TiltDetector.h"

#include <cassert>
#include <cmath>
#include <initializer_list>
#include <limits>

namespace {

using Transition = TiltDetector::Transition;
using Failure = ReferenceCollector::Failure;
constexpr UptimeMs start = UptimeMs{1} << 32;
constexpr UptimeMs interval = Config::TILT_INTERVAL_MS;
constexpr double degToRad = 3.14159265358979323846 / 180.0;
constexpr GravityReference flat{{0, 0, 1}, 1};

Result<Vec3> reading(float x, float y, float z) {
  return {{x, y, z}, Error::None};
}

Result<Vec3> incline(double deg, double azimuthDeg = 0) {
  const double angle = deg * degToRad;
  const double azimuth = azimuthDeg * degToRad;
  return reading(std::sin(angle) * std::cos(azimuth), std::sin(angle) * std::sin(azimuth), std::cos(angle));
}

TiltDetector::Observation check(TiltDetector &tilt, unsigned slot, double deg, double azimuthDeg = 0) {
  return tilt.observe(start + slot * interval, incline(deg, azimuthDeg));
}

void referenceAcquisition() {
  ReferenceCollector collector;
  assert(!collector.due(start));
  collector.start(start);
  assert(!collector.due(start - 1) && collector.due(start));
  assert(!collector.due(start + Config::ZEROING_TIME_MS));
  for (unsigned i = 0; i < 200; ++i) {
    const auto time = start + i * Config::ZEROING_SAMPLE_MS;
    if (i < 20) continue;
    collector.observe(time, reading(0, -1.02f, 0));
    collector.observe(time, reading(0, -1.02f, 0));
  }
  const auto reference = collector.finish();
  assert(reference.failure == Failure::None && reference.samples == 180);
  assert(std::abs(reference.reference.magnitudeG - 1.02) < 1e-5);
  assert(reference.reference.unit.y == -1);
  assert(reference.rmsResidualG < 1e-5);

  for (auto failure : {Failure::TooFewSamples, Failure::InvalidSample, Failure::Magnitude, Failure::Movement}) {
    collector.start(start);
    for (unsigned i = 0; i < 200; ++i) {
      if (failure == Failure::TooFewSamples && i >= 179) break;
      auto sample = reading(0, 0, 1);
      if (failure == Failure::InvalidSample && i == 10) sample.error = Error::I2cRead;
      if (failure == Failure::Magnitude) sample.value.z = 0.75f;
      if (failure == Failure::Movement) sample.value.x = i % 2 ? 0.1f : -0.1f;
      collector.observe(start + i * Config::ZEROING_SAMPLE_MS, sample);
    }
    assert(collector.finish().failure == failure);
  }
}

void geometryAndSeed() {
  TiltDetector tilt;
  constexpr double azimuths[] = {0, 90, 180, 270, 45};
  for (double azimuth : azimuths) {
    tilt.arm(start, flat);
    assert(tilt.nextDeadline() == start + interval);
    assert(check(tilt, 0, 42, azimuth).angleDeg == 0);
    const auto seeded = check(tilt, 1, 42, azimuth);
    assert(seeded.angleValid && seeded.angleDeg == 0);
    const auto combined = check(tilt, 2, 42, azimuth);
    assert(combined.angleValid && std::abs(combined.angleDeg - 42) < 1e-5);
  }
  tilt.arm(start, {{0, -1, 0}, 1.02f});
  for (unsigned slot = 1; slot <= 8; ++slot) {
    // Rotation about the gravity axis leaves its measured vector unchanged.
    const auto yaw = tilt.observe(start + slot * interval, reading(0, -1.02f, 0));
    assert(yaw.angleValid && yaw.angleDeg == 0 && yaw.transition == Transition::None);
  }
}

void boundariesAndSlowDrift() {
  TiltDetector tilt;
  tilt.arm(start, flat);
  for (unsigned slot = 1; slot <= 29; ++slot) {
    assert(check(tilt, slot, slot).transition == Transition::None);
  }
  for (unsigned slot = 30; slot <= 37; ++slot) {
    const auto actual = check(tilt, slot, 30);
    assert(actual.transition == (slot == 36 ? Transition::Detected : Transition::None));
  }
  for (unsigned slot = 38; slot <= 45; ++slot) assert(check(tilt, slot, 29).transition == Transition::None);
  assert(tilt.tilted());
  for (unsigned slot = 46; slot <= 53; ++slot) {
    const auto actual = check(tilt, slot, 27);
    assert(actual.transition == (slot == 52 ? Transition::Cleared : Transition::None));
  }
  assert(!tilt.tilted());
}

void spikesAndRearm() {
  TiltDetector tilt;
  tilt.arm(start, flat);
  assert(check(tilt, 1, 90).angleDeg == 0);
  for (unsigned slot = 2; slot <= 6; ++slot) assert(check(tilt, slot, 0).transition == Transition::None);
  for (unsigned slot = 7; slot <= 10; ++slot) assert(check(tilt, slot, 65).transition == Transition::None);
  for (unsigned slot = 11; slot <= 15; ++slot) assert(check(tilt, slot, 0).transition == Transition::None);
  for (unsigned slot = 16; slot < 22; ++slot) assert(check(tilt, slot, 65).transition == Transition::None);
  assert(check(tilt, 22, 65).transition == Transition::Detected);
  tilt.disarm();
  assert(tilt.nextDeadline() == NO_DEADLINE_MS && !tilt.tilted());
  assert(!check(tilt, 23, 65).angleValid);
  tilt.arm(start + 23 * interval, {{1, 0, 0}, 1});
  assert(check(tilt, 25, 90).angleDeg == 0);
  assert(check(tilt, 26, 90).angleDeg == 0 && !tilt.tilted());
}

void invalidAndMissedChecks() {
  for (const auto sample : {Result<Vec3>{}, reading(0, 0, 0), reading(0, 0, 0.84f), reading(0, 0, 1.16f),
                            reading(std::numeric_limits<float>::infinity(), 0, 1)}) {
    TiltDetector tilt;
    tilt.arm(start, flat);
    for (unsigned slot = 1; slot <= 6; ++slot) assert(check(tilt, slot, 45).transition == Transition::None);
    assert(!tilt.observe(start + 7 * interval, sample).angleValid);
    assert(!check(tilt, 8, 45).angleValid);
    assert(!check(tilt, 9, 45).angleValid);
    for (unsigned slot = 10; slot <= 14; ++slot) assert(check(tilt, slot, 45).transition == Transition::None);
    assert(check(tilt, 15, 45).transition == Transition::Detected);
    assert(!tilt.observe(start + 16 * interval, sample).angleValid && tilt.tilted());
  }

  TiltDetector tilt;
  tilt.arm(start, flat);
  for (unsigned slot = 1; slot <= 6; ++slot) assert(check(tilt, slot, 45).transition == Transition::None);
  assert(!check(tilt, 9, 45).angleValid && tilt.missedChecks() == 2);
  assert(!check(tilt, 9, 45).angleValid && tilt.nextDeadline() == start + 10 * interval);
  assert(!check(tilt, 10, 45).angleValid);
  for (unsigned slot = 11; slot <= 15; ++slot) assert(check(tilt, slot, 45).transition == Transition::None);
  assert(check(tilt, 16, 45).transition == Transition::Detected);
}

void persistenceUsesActualObservationTime() {
  TiltDetector tilt;
  tilt.arm(start, flat);
  check(tilt, 1, 45);
  const auto late = tilt.observe(start + 2 * interval + 100, incline(45));
  assert(late.angleValid && late.transition == Transition::None);
  for (unsigned slot = 3; slot <= 7; ++slot) assert(check(tilt, slot, 45).transition == Transition::None);
  assert(check(tilt, 8, 45).transition == Transition::Detected);
}

void hysteresisCancelsPendingTransitions() {
  TiltDetector tilt;
  tilt.arm(start, flat);
  for (unsigned slot = 1; slot <= 5; ++slot) assert(check(tilt, slot, 30).transition == Transition::None);
  for (unsigned slot = 6; slot <= 7; ++slot) assert(check(tilt, slot, 29).transition == Transition::None);
  for (unsigned slot = 8; slot <= 13; ++slot) assert(check(tilt, slot, 30).transition == Transition::None);
  assert(check(tilt, 14, 30).transition == Transition::Detected);
  for (unsigned slot = 15; slot <= 19; ++slot) assert(check(tilt, slot, 27).transition == Transition::None);
  for (unsigned slot = 20; slot <= 21; ++slot) assert(check(tilt, slot, 28).transition == Transition::None);
  for (unsigned slot = 22; slot <= 27; ++slot) assert(check(tilt, slot, 27).transition == Transition::None);
  assert(check(tilt, 28, 27).transition == Transition::Cleared);
}

}  // namespace

int main() {
  referenceAcquisition();
  geometryAndSeed();
  boundariesAndSlowDrift();
  spikesAndRearm();
  invalidAndMissedChecks();
  persistenceUsesActualObservationTime();
  hysteresisCancelsPendingTransitions();
}
