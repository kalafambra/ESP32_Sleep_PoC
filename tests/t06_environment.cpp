#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <Wire.h>

#include "EnvironmentSensor.h"
#include "config.h"

namespace {

UptimeMs now = uint64_t{1} << 33;
UptimeMs clockMs() { return now; }
constexpr uint8_t sleepControl = 0x54;

void put16(TwoWire &bus, uint8_t reg, uint16_t value) {
  bus.registers[reg] = value & 0xFF;
  bus.registers[reg + 1] = value >> 8;
}

void put20(TwoWire &bus, uint8_t reg, uint32_t value) {
  bus.registers[reg] = value >> 12;
  bus.registers[reg + 1] = value >> 4;
  bus.registers[reg + 2] = value << 4;
}

struct Fixture {
  TwoWire wire;
  EnvironmentSensor sensor{wire, clockMs};

  Fixture() {
    wire.registers[0xD0] = 0x60;
    const int coefficients[] = {27504, 26435, -1000, 36477, -10685, 3024, 2855,
                                140, -7, 15500, -14600, 6000};
    for (unsigned i = 0; i < sizeof(coefficients) / sizeof(coefficients[0]); ++i) {
      put16(wire, 0x88 + 2 * i, coefficients[i]);
    }
    wire.registers[0xA1] = 75;
    put16(wire, 0xE1, 362);
    wire.registers[0xE3] = 1;
    wire.registers[0xE4] = 0xFF;  // H4 = -10, H5 = -50, H6 = -30
    wire.registers[0xE5] = 0xE6;
    wire.registers[0xE6] = 0xFC;
    wire.registers[0xE7] = 0xE2;
  }

  void begin() {
    const auto init = sensor.begin();
    assert(init.error == EnvironmentSensor::Error::None && init.address == wire.chipAddress && init.chipId == 0x60);
    assert(wire.registers[0xF4] == sleepControl && wire.registers[0xF5] == 0 && wire.humidityLatched);
    assert(wire.triggers == 0 && wire.dataReads == 0);
  }

  void completeSample(unsigned index) {
    wire.registers[0xF3] = 0;
    wire.registers[0xF4] = sleepControl;
    put20(wire, 0xF7, 415148 + index * 2000);
    put20(wire, 0xFA, 519888 + index * 1000);
    const unsigned rawHumidity = 15000 + index * 100;
    wire.registers[0xFD] = rawHumidity >> 8;
    wire.registers[0xFE] = rawHumidity & 0xFF;
  }

  void tickAtDeadline() {
    now = sensor.nextDeadline();
    assert(now != NO_DEADLINE_MS);
    sensor.tick(now);
  }
};

void detectAndValidateIdentity() {
  Fixture primary;
  primary.wire.chipAddress = 0x76;
  primary.begin();
  Fixture wrong;
  wrong.wire.registers[0xD0] = 0x58;
  assert(wrong.sensor.begin().error == EnvironmentSensor::Error::Identity);
  assert(wrong.wire.triggers == 0);
  Fixture invalidCalibration;
  put16(invalidCalibration.wire, 0x8E, 0);
  assert(invalidCalibration.sensor.begin().error == EnvironmentSensor::Error::Calibration);
}

void fourFreshSamplesWithEarlyTicks() {
  Fixture f;
  f.begin();
  assert(f.sensor.start());
  assert(!f.sensor.start());
  EnvironmentSensor::Burst result;
  for (unsigned i = 0; i < 4; ++i) {
    const auto statusReads = f.wire.statusReads;
    const auto deadline = f.sensor.nextDeadline();
    assert(deadline == now + Config::ENV_CONVERSION_CHECK_MS);
    f.sensor.tick(now);
    f.sensor.tick(deadline - 1);
    assert(!f.sensor.takeResult(result));
    assert(f.wire.statusReads == statusReads && f.wire.dataReads == i && f.wire.triggers == i + 1);
    f.completeSample(i);
    f.tickAtDeadline();
  }
  assert(f.sensor.nextDeadline() == NO_DEADLINE_MS && f.wire.triggers == 4 && f.wire.dataReads == 4);
  assert(!f.sensor.start());  // Preserve an unread result.
  assert(f.sensor.takeResult(result));
  assert(result.error == EnvironmentSensor::Error::None && result.samples == 4);
  // Expected means for the four raw fixtures using Bosch Appendix A.1.
  assert(fabs(result.average.temperatureC - 25.55241) < 0.001);
  assert(fabs(result.average.humidityPercent - 84.94088) < 0.001);
  assert(fabs(result.average.pressureHpa - 1002.07219) < 0.001);
  assert(!f.sensor.takeResult(result));
}

void timeoutAndRecovery() {
  Fixture f;
  f.begin();
  assert(f.sensor.start());
  const auto timeout = now + Config::ENV_CONVERSION_TIMEOUT_MS;
  f.tickAtDeadline();  // Still measuring.
  assert(f.sensor.nextDeadline() == now + Config::ENV_CONVERSION_RECHECK_MS);
  assert(f.wire.triggers == 1 && f.wire.dataReads == 0);
  // Even if a late scheduler observes an idle chip, the expired sample is discarded.
  f.completeSample(0);
  now = timeout;
  f.sensor.tick(now);
  EnvironmentSensor::Burst result;
  assert(f.sensor.takeResult(result) && result.error == EnvironmentSensor::Error::Timeout);
  assert(result.samples == 0 && f.wire.dataReads == 0);
  assert(f.sensor.start() && f.wire.triggers == 2);
}

void guardStartsAfterWriteAndBusyCanClear() {
  Fixture f;
  f.begin();
  f.wire.triggerWriteMs = 17;
  const auto start = now;
  assert(f.sensor.start());
  const auto guard = start + f.wire.triggerWriteMs + Config::ENV_CONVERSION_CHECK_MS;
  assert(f.sensor.nextDeadline() == guard);
  f.wire.registers[0xF3] = 0x01;  // NVM copy can precede measuring.
  f.tickAtDeadline();
  assert(f.sensor.nextDeadline() == guard + Config::ENV_CONVERSION_RECHECK_MS);
  assert(f.wire.triggers == 1 && f.wire.dataReads == 0);
  f.completeSample(0);
  f.tickAtDeadline();
  assert(f.wire.triggers == 2 && f.wire.dataReads == 1);
}

void discardPartialOnError() {
  const bool shortReadCases[] = {false, true};
  for (bool shortRead : shortReadCases) {
    Fixture f;
    f.begin();
    assert(f.sensor.start());
    f.completeSample(0);
    f.tickAtDeadline();
    f.completeSample(1);
    f.wire.shortDataRead = shortRead;
    put20(f.wire, 0xFA, 0x80000);
    f.tickAtDeadline();
    EnvironmentSensor::Burst result;
    assert(f.sensor.takeResult(result));
    assert(result.error == (shortRead ? EnvironmentSensor::Error::Read : EnvironmentSensor::Error::InvalidSample));
    assert(result.samples == 0 && result.average.temperatureC == 0 && f.wire.triggers == 2);
  }
  Fixture failedWrite;
  failedWrite.begin();
  failedWrite.wire.rejectTrigger = true;
  assert(failedWrite.sensor.start());
  EnvironmentSensor::Burst result;
  assert(failedWrite.sensor.takeResult(result) && result.error == EnvironmentSensor::Error::Write);
  assert(failedWrite.wire.triggers == 0 && failedWrite.wire.dataReads == 0);
}

void cancelInFlightAndUnreadResult() {
  Fixture f;
  f.begin();
  assert(f.sensor.start());
  const auto deadline = f.sensor.nextDeadline();
  f.sensor.cancel();
  assert(f.sensor.nextDeadline() == NO_DEADLINE_MS);
  EnvironmentSensor::Burst result;
  assert(f.sensor.start());
  assert(f.sensor.takeResult(result) && result.error == EnvironmentSensor::Error::Busy);
  f.completeSample(0);
  now = deadline;
  f.sensor.tick(now);
  assert(f.wire.dataReads == 0 && !f.sensor.takeResult(result));

  assert(f.sensor.start());
  for (unsigned i = 0; i < 4; ++i) {
    f.completeSample(i);
    f.tickAtDeadline();
  }
  f.sensor.cancel();
  assert(!f.sensor.takeResult(result));
}

}  // namespace

void delay(unsigned long milliseconds) { now += milliseconds; }

size_t TwoWire::write(uint8_t reg) {
  reg_ = reg;
  writing_ = false;
  return 1;
}

size_t TwoWire::write(const uint8_t *bytes, size_t count) {
  assert(count == 2);
  reg_ = bytes[0];
  value_ = bytes[1];
  writing_ = true;
  return count;
}

uint8_t TwoWire::endTransmission(bool stop) {
  assert(stop == writing_);
  if (!writing_) return 0;  // ESP32 defers the repeated-start transaction to requestFrom().
  assert(address_ == chipAddress);
  if (reg_ == 0xF4 && (value_ & 3)) {
    if (rejectTrigger) return 2;
    assert((registers[0xF4] & 3) == 0);
    ++triggers;
    now += triggerWriteMs;
    registers[0xF3] = 0x08;
  }
  if (reg_ == 0xF4) humidityLatched = registers[0xF2] == 3;
  registers[reg_] = value_;
  return 0;
}

size_t TwoWire::requestFrom(uint8_t address, size_t count) {
  assert(address == address_);
  if (address != chipAddress && !(address == 0x76 && reg_ == 0xD0)) return 0;
  if (reg_ == 0xF3) ++statusReads;
  if (reg_ == 0xF7) {
    assert(count == 8 && (registers[0xF4] & 3) == 0 && registers[0xF3] == 0);
    ++dataReads;
    if (shortDataRead) return count - 1;
  }
  return count;
}

int TwoWire::read() {
  if (address_ != chipAddress && reg_ == 0xD0) {
    ++reg_;
    return primaryId;
  }
  return registers[reg_++];
}

int main() {
  detectAndValidateIdentity();
  fourFreshSamplesWithEarlyTicks();
  timeoutAndRecovery();
  guardStartsAfterWriteAndBusyCanClear();
  discardPartialOnError();
  cancelInFlightAndUnreadResult();
  puts("T06 environment tests passed");
}
