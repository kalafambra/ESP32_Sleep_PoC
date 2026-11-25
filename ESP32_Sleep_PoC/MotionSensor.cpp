#include "MotionSensor.h"

#include <Arduino.h>
#include <Wire.h>

#include "config.h"

namespace {

// InvenSense RM-MPU-6000A-00 rev 4.2, register map and sections 4.2–4.5, 4.17, 4.27–4.32.
constexpr uint8_t WHO_AM_I = 0x75;
constexpr uint8_t MPU6050_ID = 0x68;
constexpr uint8_t PWR_MGMT_1 = 0x6B;
constexpr uint8_t PWR_MGMT_2 = 0x6C;
constexpr uint8_t DEVICE_RESET = 0x80;
constexpr uint8_t SMPLRT_DIV = 0x19;
constexpr uint8_t FILTER_CONFIG = 0x1A;
constexpr uint8_t ACCEL_CONFIG = 0x1C;
constexpr uint8_t ACCEL_XOUT_H = 0x3B;
constexpr uint8_t FIFO_EN = 0x23;
constexpr uint8_t INT_ENABLE = 0x38;
constexpr uint8_t USER_CTRL = 0x6A;

constexpr uint8_t ACCEL_RANGE_8G = 2 << 3;
constexpr float ACCEL_LSB_PER_G = 4096.0f;

struct RegisterSetting {
  uint8_t address;
  uint8_t value;
};

constexpr RegisterSetting settings[] = {
    {PWR_MGMT_2, 0x07},  // Accel XYZ on, gyro XYZ standby
    {PWR_MGMT_1, 0x08},  // Wake with internal oscillator, cycle and temperature off
    {INT_ENABLE, 0x00},
    {FIFO_EN, 0x00},
    {USER_CTRL, 0x00},  // DMP, FIFO, auxiliary I2C master off
    {FILTER_CONFIG, 0x01},  // 184 Hz accel DLPF, 1 kHz rate before division
    {SMPLRT_DIV, 0x00},  // Divide by (1 + 0)
    {ACCEL_CONFIG, ACCEL_RANGE_8G},
};

constexpr float decodeAccelG(uint8_t high, uint8_t low) {
  const int32_t word = (static_cast<int32_t>(high) << 8) | low;
  const int32_t counts = word < 0x8000 ? word : word - 0x10000;
  return counts / ACCEL_LSB_PER_G;
}

static_assert(decodeAccelG(0x00, 0x00) == 0.0f && decodeAccelG(0x10, 0x00) == 1.0f &&
                  decodeAccelG(0xF0, 0x00) == -1.0f && decodeAccelG(0x80, 0x00) == -8.0f &&
                  decodeAccelG(0xFF, 0xFF) == -1.0f / ACCEL_LSB_PER_G &&
                  decodeAccelG(0x7F, 0xFF) == 32767.0f / ACCEL_LSB_PER_G,
              "MPU6050 acceleration needs signed big-endian conversion at 4096 counts/g");

}  // namespace

MotionSensor::Initialization MotionSensor::begin() {
  ready_ = false;
  Initialization report;
  static_assert(sizeof(settings) / sizeof(settings[0]) ==
                    sizeof(report.readbacks) / sizeof(report.readbacks[0]),
                "MPU readback capacity must match settings");

  // Allow register access to settle on cold power-up before probing.
  delay(Config::MPU_REGISTER_READY_WAIT_MS);
  report.registerAddress = WHO_AM_I;
  report.identity.error = readRegisters(WHO_AM_I, &report.identity.value, 1);
  if (!report.identity.valid()) {
    report.error = report.identity.error;
    return report;
  }
  if (report.identity.value != MPU6050_ID) {
    report.error = Error::UnexpectedIdentity;
    return report;
  }

  report.registerAddress = PWR_MGMT_1;
  report.error = writeRegister(PWR_MGMT_1, DEVICE_RESET);
  if (report.error != Error::None) {
    return report;
  }
  delay(Config::MPU_REGISTER_READY_WAIT_MS);

  for (const RegisterSetting &setting : settings) {
    report.registerAddress = setting.address;
    report.error = writeRegister(setting.address, setting.value);
    if (report.error != Error::None) {
      return report;
    }
    if (setting.address == PWR_MGMT_1) {
      // Let the device wake before programming its sampling and filter registers.
      delay(Config::MPU_WAKE_SETTLE_MS);
    }
  }
  delay(Config::MPU_ACCEL_SETTLE_MS);

  for (const RegisterSetting &setting : settings) {
    report.registerAddress = setting.address;
    uint8_t actual = 0;
    report.error = readRegisters(setting.address, &actual, 1);
    if (report.error != Error::None) {
      return report;
    }
    report.readbacks[report.readbackCount++] = {setting.address, setting.value, actual};
    if (actual != setting.value) {
      report.error = Error::ConfigurationMismatch;
      return report;
    }
  }

  ready_ = true;
  return report;
}

Result<Vec3> MotionSensor::readAccel() {
  Result<Vec3> result;
  if (ready_) {
    uint8_t bytes[6];
    result.error = readRegisters(ACCEL_XOUT_H, bytes, sizeof(bytes));
    if (result.valid()) {
      result.value = {decodeAccelG(bytes[0], bytes[1]), decodeAccelG(bytes[2], bytes[3]),
                      decodeAccelG(bytes[4], bytes[5])};
    }
  }
  return result;
}

Error MotionSensor::writeRegister(uint8_t reg, uint8_t value) {
  const uint8_t bytes[] = {reg, value};
  wire_.beginTransmission(Config::MPU_ADDRESS);
  const size_t queued = wire_.write(bytes, sizeof(bytes));
  const uint8_t status = wire_.endTransmission();
  return queued == sizeof(bytes) && status == 0 ? Error::None : Error::I2cWrite;
}

Error MotionSensor::readRegisters(uint8_t reg, uint8_t *bytes, size_t count) {
  wire_.beginTransmission(Config::MPU_ADDRESS);
  const bool queued = wire_.write(reg) == 1;
  // On ESP32 Wire, requestFrom executes the deferred write and repeated-start read.
  const uint8_t status = wire_.endTransmission(!queued);
  if (!queued || status != 0) {
    return Error::I2cWrite;
  }
  if (wire_.requestFrom(Config::MPU_ADDRESS, count) != count) {
    return Error::I2cRead;
  }
  for (size_t i = 0; i < count; ++i) {
    const int byte = wire_.read();
    if (byte < 0) {
      return Error::I2cRead;
    }
    bytes[i] = static_cast<uint8_t>(byte);
  }
  return Error::None;
}
