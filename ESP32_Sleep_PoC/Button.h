#pragma once

#include "Types.h"
#include "config.h"

class Button {
 public:
  // A button held at boot must be released before it can issue a command.
  constexpr Button(bool pressed = false, UptimeMs nowMs = 0)
      : changedMs_(nowMs), rawPressed_(pressed), stablePressed_(pressed), consumed_(pressed) {}

  constexpr bool tick(UptimeMs nowMs, bool pressed) {
    if (pressed != rawPressed_) {
      rawPressed_ = pressed;
      changedMs_ = nowMs;
    }
    const UptimeMs stableForMs = nowMs - changedMs_;
    if (stableForMs < Config::BUTTON_DEBOUNCE_MS) return false;

    stablePressed_ = rawPressed_;
    if (!stablePressed_) {
      consumed_ = false;
      return false;
    }
    if (consumed_ || stableForMs - Config::BUTTON_DEBOUNCE_MS < Config::BUTTON_HOLD_MS) {
      return false;
    }
    consumed_ = true;
    return true;
  }

  constexpr bool wakeMasked() const { return rawPressed_ || stablePressed_; }
  constexpr bool inputChanged(bool pressed) const { return pressed != rawPressed_; }

  // Poll while wake is masked so a release can be observed with GPIO wake disabled.
  constexpr UptimeMs nextDeadline(UptimeMs nowMs) const {
    if (!wakeMasked()) return NO_DEADLINE_MS;
    if (!rawPressed_) return changedMs_ + Config::BUTTON_DEBOUNCE_MS;
    const UptimeMs pollMs = nowMs + Config::BUTTON_DEBOUNCE_MS;
    const UptimeMs qualifyMs = changedMs_ + Config::BUTTON_DEBOUNCE_MS + Config::BUTTON_HOLD_MS;
    return !consumed_ && qualifyMs < pollMs ? qualifyMs : pollMs;
  }

 private:
  UptimeMs changedMs_;
  bool rawPressed_;
  bool stablePressed_;
  bool consumed_;
};
