#include "Button.h"
#include "DeviceController.h"

namespace {

constexpr UptimeMs start = UptimeMs{1} << 32;
constexpr UptimeMs debounce = Config::BUTTON_DEBOUNCE_MS;
constexpr UptimeMs hold = Config::BUTTON_HOLD_MS;
constexpr UptimeMs qualify = debounce + hold;
constexpr UptimeMs countdown = Config::ARMING_DELAY_MS;
constexpr UptimeMs zeroing = Config::ZEROING_TIME_MS;
using Event = DeviceController::Event;
using State = DeviceController::State;

constexpr bool ignoresBounceAndShortPress() {
  Button button;
  return !button.tick(start, true)
      && button.wakeMasked()
      && button.nextDeadline(start) == start + debounce
      && !button.tick(start + debounce - 1, false)
      && !button.wakeMasked()
      && button.nextDeadline(start + debounce - 1) == NO_DEADLINE_MS
      && !button.tick(start + debounce, true)
      && !button.tick(start + 2 * debounce, true)
      && !button.tick(start + debounce + qualify - 1, true)
      && button.nextDeadline(start + debounce + qualify - 1) == start + debounce + qualify
      && !button.tick(start + debounce + qualify - 1, false)
      && button.wakeMasked()
      && !button.tick(start + 2 * debounce + qualify - 1, false)
      && !button.wakeMasked();
}

constexpr bool restartsInterruptedQualification() {
  Button button;
  const UptimeMs resumed = start + qualify;
  return !button.tick(start, true)
      && !button.tick(start + debounce, true)
      && !button.tick(resumed - 1, false)
      && !button.tick(resumed, true)
      && !button.tick(resumed + qualify - 1, true)
      && button.tick(resumed + qualify, true);
}

constexpr bool commandsOnceUntilStableRelease() {
  Button button;
  const UptimeMs release = start + 3 * qualify;
  const UptimeMs repressed = release + debounce;
  return !button.tick(start, true)
      && !button.tick(start + qualify - 1, true)
      && button.tick(start + qualify, true)
      && !button.tick(start + 2 * qualify, false)
      && !button.tick(start + 2 * qualify + debounce - 1, true)
      && !button.tick(release - 1, true)
      && button.wakeMasked()
      && !button.tick(release, false)
      && button.nextDeadline(release) == release + debounce
      && !button.tick(release + debounce - 1, false)
      && button.wakeMasked()
      && !button.tick(repressed, false)
      && !button.wakeMasked()
      && !button.tick(repressed, true)
      && button.tick(repressed + qualify, true)
      && !button.tick(repressed + 2 * qualify, true);
}

constexpr bool requiresReleaseAfterHeldBoot() {
  Button button(true, start);
  const UptimeMs release = start + 2 * qualify;
  return button.wakeMasked()
      && !button.tick(start + qualify, true)
      && !button.tick(release, false)
      && !button.tick(release + debounce, false)
      && !button.wakeMasked()
      && !button.tick(release + debounce, true)
      && button.tick(release + debounce + qualify, true);
}

constexpr bool completesCountdownAndStartsFreshOnRearm() {
  DeviceController controller;
  const UptimeMs referenceStart = start + countdown - zeroing;
  const UptimeMs end = start + countdown;
  if (controller.state() != State::Disarmed
      || controller.nextDeadline() != NO_DEADLINE_MS
      || controller.tick(start, true, true) != Event::ArmingStarted
      || controller.state() != State::Arming
      || controller.nextDeadline() != referenceStart
      || controller.tick(referenceStart - 1, false, true) != Event::None
      || controller.tick(referenceStart, false, true) != Event::ReferenceWindowStarted
      || controller.nextDeadline() != end
      || controller.tick(end - 1, false, true) != Event::None
      || controller.tick(end, false, true) != Event::ReferenceWindowElapsed) return false;

  controller.completeArm(end, {}, false, false);
  return controller.state() == State::Armed
      && controller.nextDeadline() == NO_DEADLINE_MS
      && controller.tick(end + 1, true, true) == Event::Disarmed
      && controller.state() == State::Disarmed
      && controller.tick(end + 2, true, true) == Event::ArmingStarted
      && controller.tick(end + 2 + countdown - zeroing, false, true) == Event::ReferenceWindowStarted;
}

constexpr bool cancelsAtEitherDeadline() {
  DeviceController controller;
  const UptimeMs beforeReference = start + countdown - zeroing;
  const UptimeMs retry = start + countdown;
  return controller.tick(start, true, true) == Event::ArmingStarted
      && controller.tick(beforeReference, true, true) == Event::Disarmed
      && controller.nextDeadline() == NO_DEADLINE_MS
      && controller.tick(retry, false, true) == Event::None
      && controller.tick(retry, true, true) == Event::ArmingStarted
      && controller.tick(retry + countdown - zeroing, false, true) == Event::ReferenceWindowStarted
      && controller.tick(retry + countdown, true, true) == Event::Disarmed
      && controller.state() == State::Disarmed
      && controller.tick(retry + countdown + 1, false, true) == Event::None;
}

constexpr bool keepsFullReferenceWindowAfterLateTick() {
  DeviceController controller;
  const UptimeMs late = start + countdown;
  return controller.tick(start, true, true) == Event::ArmingStarted
      && controller.tick(late, false, true) == Event::ReferenceWindowStarted
      && controller.nextDeadline() == late + zeroing
      && controller.tick(late + zeroing - 1, false, true) == Event::None
      && controller.tick(late + zeroing, false, true) == Event::ReferenceWindowElapsed;
}

constexpr bool retriesReferenceAndAllowsCancellation() {
  DeviceController controller;
  const UptimeMs firstEnd = start + countdown;
  if (controller.tick(start, true, true) != Event::ArmingStarted
      || controller.tick(firstEnd - zeroing, false, true) != Event::ReferenceWindowStarted
      || controller.tick(firstEnd, false, true) != Event::ReferenceWindowElapsed) return false;
  controller.retryReference(firstEnd);
  if (controller.state() != State::Arming
      || controller.nextDeadline() != firstEnd + zeroing
      || controller.tick(firstEnd + zeroing - 1, false, true) != Event::None
      || controller.tick(firstEnd + zeroing, false, true) != Event::ReferenceWindowElapsed) return false;
  controller.retryReference(firstEnd + zeroing);
  return controller.tick(firstEnd + 2 * zeroing, true, true) == Event::Disarmed
      && controller.state() == State::Disarmed;
}

constexpr bool faultRequiresReset() {
  DeviceController controller;
  if (controller.tick(start, true, false) != Event::ArmRefused
      || controller.state() != State::Fault
      || controller.nextDeadline() != NO_DEADLINE_MS
      || controller.tick(start + countdown, true, true) != Event::None
      || controller.state() != State::Fault) return false;

  controller = DeviceController{};
  if (controller.state() != State::Disarmed
      || controller.tick(start, true, true) != Event::ArmingStarted) return false;
  controller.enterFault();
  return controller.state() == State::Fault
      && controller.tick(start + 2 * countdown, false, true) == Event::None;
}

static_assert(ignoresBounceAndShortPress(), "Bounce and short press must be ignored");
static_assert(restartsInterruptedQualification(), "A raw release must restart hold qualification");
static_assert(commandsOnceUntilStableRelease(), "A press must issue one command until stable release");
static_assert(requiresReleaseAfterHeldBoot(), "A button held at boot must be released before use");
static_assert(completesCountdownAndStartsFreshOnRearm(), "Countdown and rearm deadlines must be fresh");
static_assert(cancelsAtEitherDeadline(), "Cancellation must take priority at arming deadlines");
static_assert(keepsFullReferenceWindowAfterLateTick(), "A late tick must preserve the full reference window");
static_assert(retriesReferenceAndAllowsCancellation(), "Zeroing retry must keep a full window and allow cancellation");
static_assert(faultRequiresReset(), "Fault must prevent automatic rearm and reset must start disarmed");

}  // namespace
