#pragma once

#include <stddef.h>
#include <stdint.h>

// Minimal register bus used by the native EnvironmentSensor tests.
class TwoWire {
 public:
  void beginTransmission(uint8_t address) { address_ = address; }
  size_t write(uint8_t reg);
  size_t write(const uint8_t *bytes, size_t count);
  uint8_t endTransmission(bool stop = true);
  size_t requestFrom(uint8_t address, size_t count);
  int read();

  uint8_t registers[256]{};
  unsigned triggers = 0;
  unsigned dataReads = 0;
  unsigned statusReads = 0;
  unsigned triggerWriteMs = 0;
  uint8_t chipAddress = 0x77;
  uint8_t primaryId = 0x58;
  bool shortDataRead = false;
  bool rejectTrigger = false;
  bool humidityLatched = false;

 private:
  uint8_t address_ = 0;
  uint8_t reg_ = 0;
  uint8_t value_ = 0;
  bool writing_ = false;
};
