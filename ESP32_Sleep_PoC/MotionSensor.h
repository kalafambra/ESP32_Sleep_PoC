#pragma once

#include <stddef.h>

#include "Types.h"

class TwoWire;

class MotionSensor {
 public:
  struct RegisterReadback {
    uint8_t address;
    uint8_t expected;
    uint8_t actual;
  };

  struct Initialization {
    Error error = Error::NotAvailable;
    Result<uint8_t> identity;
    uint8_t registerAddress = 0;
    RegisterReadback readbacks[11]{};
    size_t readbackCount = 0;
  };

  explicit MotionSensor(TwoWire &wire) : wire_(wire) {}

  // Call during disarmed initialization with Wire already configured.
  Initialization begin();
  // Acceleration in g, in native sensor-axis order.
  Result<Vec3> readAccel();
  Error enableMotion();
  Error disableMotion();
  Error clearMotion();
  bool interruptAsserted() const;

  enum class Event { NoMotion, Reported, Suppressed };
  Result<Event> readAndAcknowledgeMotion(UptimeMs recognitionMs);
  uint64_t reportedCount() const { return reported_; }
  uint64_t suppressedCount() const { return suppressed_; }

 private:
  Error setInterruptMask(uint8_t mask);
  Result<uint8_t> acknowledgeStatus();
  Error writeRegister(uint8_t reg, uint8_t value);
  Error readRegisters(uint8_t reg, uint8_t *bytes, size_t count);

  TwoWire &wire_;
  bool ready_ = false;
  bool motionEnabled_ = false;
  uint64_t reported_ = 0;
  uint64_t suppressed_ = 0;
  UptimeMs lastReportedMs_ = 0;
};
