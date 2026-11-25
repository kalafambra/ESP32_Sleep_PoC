#include <Arduino.h>
#include <Wire.h>
#include <esp_arduino_version.h>
#include <esp_wifi.h>
#include <math.h>

#include "MotionSensor.h"
#include "Uptime.h"
#include "config.h"

namespace {

constexpr bool statusLedEnabled = Config::ENABLE_STATUS_LED && Config::STATUS_LED_PIN >= 0;
constexpr bool uptimeTestEnabled = Config::TEST_MODE == Config::TestMode::FoundationUptime;
constexpr bool accelTestEnabled = Config::TEST_MODE == Config::TestMode::MpuAcceleration;
MotionSensor motionSensor(Wire);
UptimeMs nextUptimeLogMs = 0;
UptimeMs nextAccelLogMs = 0;
bool accelDiagnosticRunning = false;
uint8_t consecutiveAccelErrors = 0;

const char *onOff(bool enabled) {
  return enabled ? "on" : "off";
}

void setStatusLed(bool on) {
  if (statusLedEnabled) {
    digitalWrite(Config::STATUS_LED_PIN, on ? HIGH : LOW);
  }
}

void printTimestamp(UptimeMs nowMs) {
  char timestamp[UPTIME_TEXT_SIZE];
  formatUptime(nowMs, timestamp);
  Serial0.printf("[%s] ", timestamp);
}

const char *errorText(Error error) {
  switch (error) {
    case Error::None: return "OK";
    case Error::NotAvailable: return "MPU6050 unavailable";
    case Error::I2cWrite: return "I2C write failed";
    case Error::I2cRead: return "I2C read failed or incomplete";
    case Error::UnexpectedIdentity: return "unexpected MPU6050 identity";
    case Error::ConfigurationMismatch: return "MPU6050 register readback mismatch";
  }
  return "unknown error";
}

void printMpuStartup(bool wireReady, const MotionSensor::Initialization &mpu) {
  if (mpu.identity.valid()) {
    Serial0.printf("MPU address 0x%02X | WHO_AM_I 0x%02X\n",
                   Config::MPU_ADDRESS, mpu.identity.value);
  }
  if (mpu.readbackCount > 0) {
    Serial0.print("MPU register readback (register: actual/expected)");
    for (size_t i = 0; i < mpu.readbackCount; ++i) {
      const auto &entry = mpu.readbacks[i];
      Serial0.printf(" 0x%02X:0x%02X/0x%02X", entry.address, entry.actual, entry.expected);
    }
    Serial0.println();
  }
  if (mpu.error == Error::None) {
    Serial0.println("MPU6050 ready | XYZ +/-8 g | nominal 1 kHz | accel DLPF 184 Hz");
  } else {
    printTimestamp(uptimeMs());
    if (wireReady) {
      Serial0.printf("MPU_INIT_ERROR | address 0x%02X | register 0x%02X | %s\n",
                     Config::MPU_ADDRESS, mpu.registerAddress, errorText(mpu.error));
    } else {
      Serial0.println("MPU_INIT_ERROR | shared I2C unavailable");
    }
  }
}

void printStartupBanner(bool cpuConfigured, bool wireReady, bool wifiOff, bool bluetoothOff,
                        const MotionSensor::Initialization &mpu) {
  Serial0.printf("ESP32 Sleep PoC | T02 MPU6050 acceleration | Arduino-ESP32 %s\n", ESP_ARDUINO_VERSION_STR);
  const char *mode = uptimeTestEnabled ? "foundation uptime test" : "normal";
  if (accelTestEnabled) {
    mode = "MPU acceleration test";
  }
  Serial0.printf("Mode %s | CPU target %lu MHz, actual %lu MHz | UART %lu baud (TX GPIO%d, RX GPIO%d)\n",
                 mode,
                 static_cast<unsigned long>(Config::CPU_FREQ_MHZ), static_cast<unsigned long>(getCpuFrequencyMhz()),
                 static_cast<unsigned long>(Config::SERIAL_BAUD), Config::UART_TX_PIN, Config::UART_RX_PIN);
  Serial0.printf("I2C SDA GPIO%d, SCL GPIO%d, target %lu Hz, timeout %u ms | %s\n",
                 Config::I2C_SDA_PIN, Config::I2C_SCL_PIN, static_cast<unsigned long>(Config::I2C_CLOCK_HZ),
                 static_cast<unsigned>(Config::I2C_TIMEOUT_MS), wireReady ? "initialized" : "initialization failed");
  Serial0.printf("BOOT GPIO%d active LOW | LED GPIO%d active HIGH (%s) | MPU INT GPIO%d active HIGH\n",
                 Config::BOOT_PIN, Config::STATUS_LED_PIN, onOff(statusLedEnabled), Config::MPU_INT_PIN);
  printMpuStartup(wireReady, mpu);
  Serial0.printf("BME280 identity check pending | expected at 0x%02X or 0x%02X\n",
                 static_cast<unsigned>(Config::BME_PRIMARY_ADDRESS),
                 static_cast<unsigned>(Config::BME_SECONDARY_ADDRESS));
  Serial0.printf("Feature settings | Impact %s | Tilt %s | Environment %s | Sleep %s | Status LED %s\n",
                 onOff(Config::ENABLE_IMPACT_DETECTION), onOff(Config::ENABLE_TILT_DETECTION),
                 onOff(Config::ENABLE_ENV_ACQUISITION), onOff(Config::ENABLE_SLEEP), onOff(statusLedEnabled));
  Serial0.println("T02 operation | Monitoring and light sleep unavailable | CPU awake");
  Serial0.printf("Radios | Wi-Fi %s | Bluetooth %s\n", wifiOff ? "off" : "shutdown failed",
                 bluetoothOff ? "off" : "shutdown failed");

  printTimestamp(uptimeMs());
  Serial0.println("DISARMED");
  if (!cpuConfigured || !wireReady || !wifiOff || !bluetoothOff) {
    printTimestamp(uptimeMs());
    Serial0.printf("STARTUP_FAULT | CPU configuration %s | I2C initialization %s | Radio shutdown %s\n",
                   cpuConfigured ? "OK" : "failed", wireReady ? "OK" : "failed",
                   wifiOff && bluetoothOff ? "OK" : "failed");
  }
}

void logAcceleration(UptimeMs nowMs) {
  const auto sample = motionSensor.readAccel();
  printTimestamp(nowMs);
  if (!sample.valid()) {
    ++consecutiveAccelErrors;
    Serial0.printf("MPU_ACCEL_ERROR | %s | consecutive failures %u/%u\n", errorText(sample.error),
                   consecutiveAccelErrors, Config::SENSOR_FAILURE_LIMIT);
    if (consecutiveAccelErrors >= Config::SENSOR_FAILURE_LIMIT) {
      accelDiagnosticRunning = false;
      printTimestamp(nowMs);
      Serial0.println("MPU_ACCEL_FAULT | diagnostic stopped | DISARMED | reset after checking sensor");
    }
    return;
  }

  consecutiveAccelErrors = 0;
  const Vec3 &a = sample.value;
  const float magnitudeG = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
  Serial0.printf("MPU_ACCEL | X %+.3f g | Y %+.3f g | Z %+.3f g | magnitude %.3f g\n",
                 a.x, a.y, a.z, magnitudeG);
}

}  // namespace

void setup() {
  const esp_err_t wifiStopResult = esp_wifi_stop();
  const bool wifiOff = wifiStopResult == ESP_OK || wifiStopResult == ESP_ERR_WIFI_NOT_INIT;
  const bool bluetoothOff = btStop();
  const bool cpuConfigured = setCpuFrequencyMhz(Config::CPU_FREQ_MHZ);
  Serial0.begin(Config::SERIAL_BAUD, SERIAL_8N1, Config::UART_RX_PIN, Config::UART_TX_PIN);

  pinMode(Config::BOOT_PIN, INPUT_PULLUP);
  pinMode(Config::MPU_INT_PIN, INPUT);
  if (statusLedEnabled) {
    pinMode(Config::STATUS_LED_PIN, OUTPUT);
    setStatusLed(false);
  }

  const bool wireReady = Wire.begin(Config::I2C_SDA_PIN, Config::I2C_SCL_PIN, Config::I2C_CLOCK_HZ);
  Wire.setTimeOut(Config::I2C_TIMEOUT_MS);
  const auto mpu = wireReady ? motionSensor.begin() : MotionSensor::Initialization{};
  printStartupBanner(cpuConfigured, wireReady, wifiOff, bluetoothOff, mpu);
  const UptimeMs nowMs = uptimeMs();
  nextUptimeLogMs = nowMs + Config::FOUNDATION_UPTIME_LOG_MS;
  nextAccelLogMs = nowMs + Config::MPU_ACCEL_LOG_MS;
  accelDiagnosticRunning = accelTestEnabled && mpu.error == Error::None;
}

void loop() {
  if (uptimeTestEnabled) {
    const UptimeMs nowMs = uptimeMs();
    if (nowMs >= nextUptimeLogMs) {
      printTimestamp(nowMs);
      Serial0.println("UPTIME DISARMED");
      nextUptimeLogMs = nowMs + Config::FOUNDATION_UPTIME_LOG_MS;
    }
  }
  if (accelTestEnabled && accelDiagnosticRunning) {
    const UptimeMs nowMs = uptimeMs();
    if (nowMs >= nextAccelLogMs) {
      logAcceleration(nowMs);
      nextAccelLogMs = nowMs + Config::MPU_ACCEL_LOG_MS;
    }
  }
  delay(Config::AWAKE_IDLE_YIELD_MS);
}
