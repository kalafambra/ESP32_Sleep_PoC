#include "EnvironmentSensor.h"

#include <Arduino.h>
#include <Wire.h>
#include <math.h>

#include "config.h"

namespace {

// Bosch BST-BME280-DS001-24, sections 4–5 and appendix A.
constexpr uint8_t CHIP_ID = 0xD0;
constexpr uint8_t BME280_ID = 0x60;
constexpr uint8_t RESET = 0xE0;
constexpr uint8_t RESET_COMMAND = 0xB6;
constexpr uint8_t TEMP_PRESS_CALIBRATION = 0x88;
constexpr uint8_t HUMIDITY_CALIBRATION = 0xE1;
constexpr uint8_t CTRL_HUM = 0xF2;
constexpr uint8_t STATUS_REGISTER = 0xF3;
constexpr uint8_t CTRL_MEAS = 0xF4;
constexpr uint8_t CONFIG = 0xF5;
constexpr uint8_t DATA = 0xF7;
constexpr uint8_t NVM_COPY = 0x01;
constexpr uint8_t MEASURING = 0x08;
constexpr uint8_t MODE_MASK = 0x03;
constexpr uint8_t FORCED_MODE = 0x01;

constexpr uint8_t oversamplingBits(uint8_t multiplier) {
  return multiplier == 1 ? 1 : 1 + oversamplingBits(multiplier / 2);
}

constexpr uint8_t HUM_OVERSAMPLING = oversamplingBits(Config::ENV_HUM_OVERSAMPLING);
constexpr uint8_t SLEEP_CONTROL = (oversamplingBits(Config::ENV_TEMP_OVERSAMPLING) << 5) |
                                  (oversamplingBits(Config::ENV_PRES_OVERSAMPLING) << 2);

uint16_t littleEndian(const uint8_t *bytes) {
  return bytes[0] | (bytes[1] << 8);
}

int16_t signExtend(uint16_t value, int bits) {
  return value < (1 << (bits - 1)) ? value : static_cast<int>(value) - (1 << bits);
}

uint32_t readAdc20(const uint8_t *bytes) {
  return (static_cast<uint32_t>(bytes[0]) << 12) | (bytes[1] << 4) | (bytes[2] >> 4);
}

}  // namespace

EnvironmentSensor::Initialization EnvironmentSensor::begin() {
  cancel();
  ready_ = false;
  Initialization report;
  const uint8_t addresses[] = {Config::BME_PRIMARY_ADDRESS, Config::BME_SECONDARY_ADDRESS};
  for (uint8_t address : addresses) {
    address_ = address;
    uint8_t chipId = 0;
    if (readRegisters(CHIP_ID, &chipId, 1) != Error::None) continue;
    report = {Error::Identity, address, chipId};
    if (chipId != BME280_ID) continue;
    report.error = configure();
    ready_ = report.error == Error::None;
    break;
  }
  return report;
}

EnvironmentSensor::Error EnvironmentSensor::configure() {
  Error error = writeRegister(RESET, RESET_COMMAND);
  if (error != Error::None) return error;
  delay(Config::ENV_RESET_SETTLE_MS);  // Setup only, before arming.
  uint8_t status = 0;
  error = readRegisters(STATUS_REGISTER, &status, 1);
  if (error != Error::None) return error;
  if (status & NVM_COPY) return Error::Busy;

  uint8_t tp[26], h[7];
  error = readRegisters(TEMP_PRESS_CALIBRATION, tp, sizeof(tp));
  if (error != Error::None) return error;
  error = readRegisters(HUMIDITY_CALIBRATION, h, sizeof(h));
  if (error != Error::None) return error;
  calibration_ = {
      littleEndian(tp), littleEndian(tp + 6),
      signExtend(littleEndian(tp + 2), 16), signExtend(littleEndian(tp + 4), 16),
      signExtend(littleEndian(tp + 8), 16), signExtend(littleEndian(tp + 10), 16),
      signExtend(littleEndian(tp + 12), 16), signExtend(littleEndian(tp + 14), 16),
      signExtend(littleEndian(tp + 16), 16), signExtend(littleEndian(tp + 18), 16),
      signExtend(littleEndian(tp + 20), 16), signExtend(littleEndian(tp + 22), 16),
      tp[25], h[2], signExtend(littleEndian(h), 16),
      signExtend((h[3] << 4) | (h[4] & 0x0F), 12),
      signExtend((h[5] << 4) | (h[4] >> 4), 12),
      static_cast<int8_t>(signExtend(h[6], 8))};
  if (calibration_.t1 == 0 || calibration_.t1 == 0xFFFF ||
      calibration_.p1 == 0 || calibration_.p1 == 0xFFFF) return Error::Calibration;

  // Reset leaves the device asleep. Writing CTRL_MEAS latches CTRL_HUM.
  error = writeRegister(CTRL_HUM, HUM_OVERSAMPLING);
  if (error != Error::None) return error;
  error = writeRegister(CONFIG, 0);
  if (error != Error::None) return error;
  error = writeRegister(CTRL_MEAS, SLEEP_CONTROL);
  if (error != Error::None) return error;
  uint8_t readback[4];
  error = readRegisters(CTRL_HUM, readback, sizeof(readback));
  if (error != Error::None) return error;
  return (readback[0] & 0x07) == HUM_OVERSAMPLING &&
             readback[2] == SLEEP_CONTROL && (readback[3] & 0xFD) == 0
         ? Error::None : Error::Configuration;
}

bool EnvironmentSensor::start() {
  if (deadlineMs_ != NO_DEADLINE_MS || resultPending_) return false;
  burst_ = {};
  const Error error = ready_ ? trigger() : Error::Unavailable;
  if (error != Error::None) finish(error);
  return true;
}

EnvironmentSensor::Error EnvironmentSensor::trigger() {
  uint8_t state[2];
  Error error = readRegisters(STATUS_REGISTER, state, sizeof(state));
  if (error != Error::None) return error;
  if ((state[0] & (MEASURING | NVM_COPY)) || (state[1] & MODE_MASK)) return Error::Busy;
  if (state[1] != SLEEP_CONTROL) return Error::Configuration;
  error = writeRegister(CTRL_MEAS, SLEEP_CONTROL | FORCED_MODE);
  if (error != Error::None) return error;
  const UptimeMs triggeredMs = clock_();
  deadlineMs_ = triggeredMs + Config::ENV_CONVERSION_CHECK_MS;
  timeoutMs_ = triggeredMs + Config::ENV_CONVERSION_TIMEOUT_MS;
  return Error::None;
}

void EnvironmentSensor::tick(UptimeMs nowMs) {
  if (deadlineMs_ == NO_DEADLINE_MS || nowMs < deadlineMs_) return;
  if (nowMs >= timeoutMs_) {
    finish(Error::Timeout);
    return;
  }
  uint8_t state[2];
  const Error statusError = readRegisters(STATUS_REGISTER, state, sizeof(state));
  if (statusError != Error::None) {
    finish(statusError);
    return;
  }
  const UptimeMs checkedMs = clock_();
  if (checkedMs >= timeoutMs_) {
    finish(Error::Timeout);
    return;
  }
  if ((state[1] & ~MODE_MASK) != SLEEP_CONTROL) {
    finish(Error::Configuration);
    return;
  }
  if ((state[0] & (MEASURING | NVM_COPY)) || (state[1] & MODE_MASK)) {
    const UptimeMs retryMs = checkedMs + Config::ENV_CONVERSION_RECHECK_MS;
    deadlineMs_ = retryMs < timeoutMs_ ? retryMs : timeoutMs_;
    return;
  }

  Measurement sample;
  const Error error = readSample(sample);
  if (error != Error::None || clock_() >= timeoutMs_) {
    finish(error != Error::None ? error : Error::Timeout);
    return;
  }
  burst_.average.temperatureC += sample.temperatureC;
  burst_.average.humidityPercent += sample.humidityPercent;
  burst_.average.pressureHpa += sample.pressureHpa;
  if (++burst_.samples == Config::ENV_SAMPLE_COUNT) {
    burst_.average.temperatureC /= burst_.samples;
    burst_.average.humidityPercent /= burst_.samples;
    burst_.average.pressureHpa /= burst_.samples;
    finish(Error::None);
    return;
  }
  const Error triggerError = trigger();
  if (triggerError != Error::None) finish(triggerError);
}

EnvironmentSensor::Error EnvironmentSensor::readSample(Measurement &sample) {
  uint8_t bytes[8];
  const Error error = readRegisters(DATA, bytes, sizeof(bytes));
  if (error != Error::None) return error;
  const uint32_t rawPressure = readAdc20(bytes);
  const uint32_t rawTemperature = readAdc20(bytes + 3);
  const uint16_t rawHumidity = (bytes[6] << 8) | bytes[7];
  if (rawPressure == 0x80000 || rawTemperature == 0x80000 || rawHumidity == 0x8000) {
    return Error::InvalidSample;
  }

  // Appendix A.1 compensation. Each channel uses the same conversion's temperature.
  const Calibration &c = calibration_;
  const double t = rawTemperature / 16384.0 - c.t1 / 1024.0;
  const double tDelta = rawTemperature / 131072.0 - c.t1 / 8192.0;
  const double fine = t * c.t2 + tDelta * tDelta * c.t3;
  const double tFine = static_cast<int32_t>(fine);
  sample.temperatureC = fine / 5120.0;

  const double pDelta = tFine / 2.0 - 64000.0;
  const double pressureOffset = (pDelta * pDelta * c.p6 / 32768.0 + 2.0 * pDelta * c.p5) / 4.0 +
                                c.p4 * 65536.0;
  const double pressureScale =
      (1.0 + (c.p3 * pDelta * pDelta / 524288.0 + c.p2 * pDelta) / 524288.0 / 32768.0) * c.p1;
  if (pressureScale <= 0.0) return Error::InvalidSample;
  const double pressurePa = ((1048576.0 - rawPressure) - pressureOffset / 4096.0) *
                            6250.0 / pressureScale;
  sample.pressureHpa =
      (pressurePa + (c.p9 * pressurePa * pressurePa / 2147483648.0 +
                     c.p8 * pressurePa / 32768.0 + c.p7) / 16.0) / 100.0;

  const double hDelta = tFine - 76800.0;
  const double humidity = (rawHumidity - (c.h4 * 64.0 + c.h5 / 16384.0 * hDelta)) *
                          (c.h2 / 65536.0 * (1.0 + c.h6 / 67108864.0 * hDelta *
                           (1.0 + c.h3 / 67108864.0 * hDelta)));
  const double correctedHumidity = humidity * (1.0 - c.h1 * humidity / 524288.0);
  if (!isfinite(sample.temperatureC) || !isfinite(sample.pressureHpa) ||
      sample.pressureHpa <= 0.0 || !isfinite(correctedHumidity)) return Error::InvalidSample;
  sample.humidityPercent = correctedHumidity < 0.0 ? 0.0 : correctedHumidity > 100.0 ? 100.0 : correctedHumidity;
  return Error::None;
}

void EnvironmentSensor::finish(Error error) {
  if (error != Error::None) burst_ = {};
  burst_.error = error;
  deadlineMs_ = NO_DEADLINE_MS;
  resultPending_ = true;
}

bool EnvironmentSensor::takeResult(Burst &result) {
  if (!resultPending_) return false;
  result = burst_;
  resultPending_ = false;
  return true;
}

void EnvironmentSensor::cancel() {
  // The chip returns to sleep on its own after an in-flight forced conversion.
  deadlineMs_ = NO_DEADLINE_MS;
  resultPending_ = false;
  burst_ = {};
}

EnvironmentSensor::Error EnvironmentSensor::writeRegister(uint8_t reg, uint8_t value) {
  const uint8_t bytes[] = {reg, value};
  wire_.beginTransmission(address_);
  const size_t queued = wire_.write(bytes, sizeof(bytes));
  const uint8_t status = wire_.endTransmission();
  return queued == sizeof(bytes) && status == 0 ? Error::None : Error::Write;
}

EnvironmentSensor::Error EnvironmentSensor::readRegisters(uint8_t reg, uint8_t *data, size_t count) {
  wire_.beginTransmission(address_);
  const bool queued = wire_.write(reg) == 1;
  const uint8_t status = wire_.endTransmission(!queued);
  if (!queued || status != 0) return Error::Write;
  if (wire_.requestFrom(address_, count) != count) return Error::Read;
  for (size_t i = 0; i < count; ++i) {
    const int byte = wire_.read();
    if (byte < 0) return Error::Read;
    data[i] = static_cast<uint8_t>(byte);
  }
  return Error::None;
}

const char *EnvironmentSensor::errorText(Error error) {
  switch (error) {
    case Error::None: return "OK";
    case Error::Unavailable: return "BME280 unavailable";
    case Error::Identity: return "unexpected chip ID";
    case Error::Read: return "I2C read failed or incomplete";
    case Error::Write: return "I2C write failed";
    case Error::Busy: return "sensor busy";
    case Error::Configuration: return "configuration mismatch";
    case Error::Calibration: return "invalid calibration coefficients";
    case Error::Timeout: return "conversion timed out";
    case Error::InvalidSample: return "invalid sample";
  }
  return "unknown error";
}
