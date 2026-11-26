#pragma once

#include <stddef.h>

#include "Types.h"

class TwoWire;

class EnvironmentSensor {
 public:
  enum class Error {
    None, Unavailable, Identity, Read, Write, Busy, Configuration, Calibration, Timeout, InvalidSample
  };

  struct Initialization {
    Error error = Error::Unavailable;
    uint8_t address = 0;
    uint8_t chipId = 0;
  };

  struct Measurement {
    double temperatureC = 0;
    double humidityPercent = 0;
    double pressureHpa = 0;
  };

  struct Burst {
    Error error = Error::None;
    Measurement average;
    uint8_t samples = 0;
  };

  using Clock = UptimeMs (*)();
  constexpr EnvironmentSensor(TwoWire &wire, Clock clock) : wire_(wire), clock_(clock) {}

  Initialization begin();
  // Accept one burst at a time. A rejected hardware operation becomes a result.
  bool start();
  void tick(UptimeMs nowMs);
  UptimeMs nextDeadline() const { return deadlineMs_; }
  bool takeResult(Burst &result);
  void cancel();
  static const char *errorText(Error error);

 private:
  struct Calibration {
    uint16_t t1, p1;
    int16_t t2, t3, p2, p3, p4, p5, p6, p7, p8, p9;
    uint8_t h1, h3;
    int16_t h2, h4, h5;
    int8_t h6;
  };

  Error configure();
  Error trigger();
  Error readSample(Measurement &sample);
  Error readRegisters(uint8_t reg, uint8_t *data, size_t count);
  Error writeRegister(uint8_t reg, uint8_t value);
  void finish(Error error);

  TwoWire &wire_;
  Clock clock_;
  uint8_t address_ = 0;
  bool ready_ = false;
  Calibration calibration_{};
  UptimeMs deadlineMs_ = NO_DEADLINE_MS;
  UptimeMs timeoutMs_ = 0;
  Burst burst_{};
  bool resultPending_ = false;
};
