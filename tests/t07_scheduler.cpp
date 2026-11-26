#include <cassert>
#include <string>

#include "DeviceController.h"

namespace {

constexpr UptimeMs start = (UptimeMs{1} << 32) + 100;
constexpr UptimeMs tiltPeriod = Config::TILT_INTERVAL_MS;
constexpr UptimeMs envPeriod = Config::ENV_INTERVAL_MS;
constexpr UptimeMs qualify = Config::BUTTON_DEBOUNCE_MS + Config::BUTTON_HOLD_MS;
constexpr GravityReference flat{{0, 0, 1}, 1};
constexpr Result<Vec3> flatSample{{0, 0, 1}, Error::None};
constexpr Result<Vec3> tiltedSample{{0.7071068f, 0, 0.7071068f}, Error::None};

struct FakeIo {
  DeviceController controller;
  UptimeMs time = start;
  UptimeMs conversionAt = NO_DEADLINE_MS;
  UptimeMs conversionDuration = 0;
  UptimeMs retryAt = 0;
  UptimeMs skipped = 0;
  UptimeMs totalSkipped = 0;
  bool pressed = false;
  bool motion = false;
  bool result = false;
  bool reportRaisesMotion = false;
  bool reportChangesBoot = false;
  Result<Vec3> sample = flatSample;
  std::string calls;

  DeviceController::IdleState run(UptimeMs at) {
    assert(at >= time);
    time = at;
    calls.clear();
    return controller.run(*this);
  }

  UptimeMs now() const { return time; }
  bool bootPressed() const { return pressed; }
  bool motionEnabled() const { return true; }
  bool motionAsserted() const { return motion; }
  UptimeMs motionRetryAt() const { return retryAt; }
  bool readyToArm() const { return true; }
  void serviceMotion(UptimeMs) {
    calls += 'M';
    motion = false;
    retryAt = 0;
  }

  void lifecycleEvent(DeviceController::Event event, UptimeMs at, bool wasArmed) {
    switch (event) {
      case DeviceController::Event::ArmingStarted: calls += 'A'; break;
      case DeviceController::Event::ReferenceWindowStarted:
        calls += 'Z';
        controller.reference().start(at);
        break;
      case DeviceController::Event::ReferenceWindowElapsed: {
        calls += 'G';
        const auto outcome = controller.reference().finish();
        assert(outcome.failure == ReferenceCollector::Failure::None);
        controller.completeArm(at, outcome.reference, true, true);
        break;
      }
      case DeviceController::Event::Disarmed:
        assert(wasArmed);
        calls += 'D';
        motion = false;
        conversionAt = NO_DEADLINE_MS;
        result = false;
        break;
      default: assert(false);
    }
  }

  void sampleReference(UptimeMs at) {
    calls += 'R';
    controller.reference().observe(at, sample);
  }
  void checkTilt(UptimeMs at) {
    calls += 'T';
    const auto observation = controller.tilt().observe(at, sample);
    if (observation.transition == TiltDetector::Transition::Detected) calls += '!';
  }

  UptimeMs environmentDeadline() const { return conversionAt; }
  bool startEnvironment(UptimeMs at) {
    if (conversionAt != NO_DEADLINE_MS || result) return false;
    calls += 'E';
    conversionAt = at + Config::ENV_CONVERSION_CHECK_MS;
    return true;
  }
  void advanceEnvironment(UptimeMs) {
    calls += 'C';
    time += conversionDuration;
    conversionAt = NO_DEADLINE_MS;
    result = true;
  }
  void environmentMissed(UptimeMs, UptimeMs slots, UptimeMs total) {
    calls += 'S';
    skipped = slots;
    totalSkipped = total;
  }
  void report() {
    calls += 'P';
    if (result) {
      calls += 'B';
      result = false;
    }
    if (reportRaisesMotion) motion = true;
    if (reportChangesBoot) pressed = true;
  }
};

void concurrentWorkAndEarlyWake() {
  FakeIo io;
  io.controller.completeArm(start, flat, true, true);
  assert(io.run(start).deadlineMs == start + tiltPeriod);
  io.motion = true;
  assert(io.run(start + 50).deadlineMs == start + tiltPeriod);
  assert(io.calls == "M");

  const UptimeMs burst = start + envPeriod;
  io.motion = true;
  const auto waiting = io.run(burst);
  assert(io.calls == "MTEP");
  assert(waiting.deadlineMs == burst + Config::ENV_CONVERSION_CHECK_MS);
  io.motion = true;
  assert(io.run(burst + 1).deadlineMs == waiting.deadlineMs);
  assert(io.calls == "M");
  assert(io.run(waiting.deadlineMs).deadlineMs == burst + tiltPeriod);
  assert(io.calls == "CPB");
}

void missedSlotsAndPersistence() {
  FakeIo io;
  io.controller.completeArm(start, flat, true, true);
  io.sample = tiltedSample;
  io.run(start + tiltPeriod);
  io.run(start + 2 * tiltPeriod);
  const UptimeMs late = start + 2 * envPeriod;
  io.run(late);
  assert(io.calls == "TSP");
  assert(io.skipped == 2 && io.totalSkipped == 2);
  assert(io.controller.tilt().missedChecks() == (late - start) / tiltPeriod - 3);
  const UptimeMs firstFiltered = late + (Config::TILT_FILTER_SAMPLES - 1) * tiltPeriod;
  for (UptimeMs at = late + tiltPeriod; at < firstFiltered + Config::TILT_PERSISTENCE_MS; at += tiltPeriod) {
    io.run(at);
    assert(io.calls == "TP");
  }
  io.run(firstFiltered + Config::TILT_PERSISTENCE_MS);
  assert(io.calls == "T!P");

  const UptimeMs nextBurst = start + 3 * envPeriod;
  io.run(nextBurst);
  assert(io.calls == "TEP");
  // An unfinished burst also costs a slot, while the pending conversion can finish.
  io.conversionAt = nextBurst + envPeriod;
  io.run(nextBurst + envPeriod);
  assert(io.calls == "TSCPB");
  assert(io.skipped == 1 && io.totalSkipped == 3);
}

void disarmAndRearm() {
  FakeIo io;
  io.controller.completeArm(start, flat, true, true);
  const UptimeMs firstBurst = start + envPeriod;
  io.pressed = true;
  io.run(firstBurst - qualify + 1);
  io.run(firstBurst);
  assert(io.calls == "TEP");
  // Qualify BOOT during the last I/O step. No finished result may escape disarm.
  io.conversionAt = firstBurst;
  io.conversionDuration = 1;
  io.run(firstBurst);
  assert(io.calls == "CD" && !io.result);
  assert(io.controller.state() == DeviceController::State::Disarmed);
  io.run(firstBurst + envPeriod);
  assert(io.calls.empty());

  io.pressed = false;
  const UptimeMs release = io.time;
  io.run(release);
  io.run(release + Config::BUTTON_DEBOUNCE_MS);
  io.pressed = true;
  io.sample = tiltedSample;
  const UptimeMs newPress = io.time;
  io.run(newPress);
  const UptimeMs arming = newPress + qualify;
  const UptimeMs zeroing = arming + Config::ARMING_DELAY_MS - Config::ZEROING_TIME_MS;
  assert(io.run(arming).deadlineMs < zeroing && io.calls == "A");
  io.pressed = false;
  io.run(arming + 1);
  assert(io.run(arming + 1 + Config::BUTTON_DEBOUNCE_MS).deadlineMs == zeroing);
  assert(io.run(zeroing).deadlineMs == zeroing + Config::ZEROING_SAMPLE_MS && io.calls == "ZR");
  const UptimeMs rearmed = zeroing + Config::ZEROING_TIME_MS;
  for (UptimeMs at = zeroing + Config::ZEROING_SAMPLE_MS; at < rearmed; at += Config::ZEROING_SAMPLE_MS) {
    io.run(at);
    assert(io.calls == "R");
  }
  assert(io.run(rearmed).deadlineMs == rearmed + tiltPeriod && io.calls == "G");
  for (UptimeMs at = rearmed + tiltPeriod; at < rearmed + envPeriod; at += tiltPeriod) {
    io.run(at);
    assert(io.calls == "TP");
  }
  io.run(rearmed + envPeriod);
  assert(io.calls == "TEP" && !io.controller.tilt().tilted());
}

void inputPreemptionAndIdleRecheck() {
  FakeIo io;
  io.controller.completeArm(start, flat, true, true);
  io.reportRaisesMotion = true;
  const UptimeMs check = start + tiltPeriod;
  assert(io.run(check).deadlineMs == check && io.calls == "TP");
  io.run(check);
  assert(io.calls == "M");

  io.motion = true;
  io.retryAt = check + Config::MOTION_RETRY_MS;
  auto idle = io.run(check);
  assert(idle.deadlineMs == io.retryAt && !idle.motionWakeEnabled && idle.bootWakeEnabled);
  io.motion = false;
  idle = io.run(io.retryAt);
  assert(io.calls == "M" && idle.motionWakeEnabled);

  const UptimeMs burst = start + envPeriod;
  io.pressed = true;
  idle = io.run(burst - qualify);
  assert(!idle.bootWakeEnabled && idle.motionWakeEnabled);
  io.motion = true;
  io.run(burst);
  assert(io.calls == "MD");

  FakeIo bootIo;
  bootIo.controller.completeArm(start, flat, true, false);
  bootIo.reportChangesBoot = true;
  assert(bootIo.run(check).deadlineMs == check);
  assert(!bootIo.run(check).bootWakeEnabled);
}

}  // namespace

int main() {
  concurrentWorkAndEarlyWake();
  missedSlotsAndPersistence();
  disarmAndRearm();
  inputPreemptionAndIdleRecheck();
}
