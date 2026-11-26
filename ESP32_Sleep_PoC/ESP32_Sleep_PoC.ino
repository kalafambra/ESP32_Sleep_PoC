#include <Arduino.h>
#include <Wire.h>
#include <esp_arduino_version.h>
#include <esp_wifi.h>
#include <math.h>

#include "Button.h"
#include "DeviceController.h"
#include "MotionSensor.h"
#include "Uptime.h"
#include "config.h"

namespace {

constexpr bool statusLedEnabled = Config::ENABLE_STATUS_LED && Config::STATUS_LED_PIN >= 0;
constexpr bool uptimeTestEnabled = Config::TEST_MODE == Config::TestMode::FoundationUptime;
constexpr bool accelTestEnabled = Config::TEST_MODE == Config::TestMode::MpuAcceleration;
constexpr bool latchTestEnabled = Config::TEST_MODE == Config::TestMode::MotionLatch;
constexpr bool lifecycleTestEnabled = Config::TEST_MODE == Config::TestMode::ButtonLifecycle;
MotionSensor motionSensor(Wire);
Button bootButton;
DeviceController lifecycle;
const char *armingBlockReason = nullptr;
UptimeMs nextUptimeLogMs = 0;
UptimeMs nextAccelLogMs = 0;
bool accelDiagnosticRunning = false;
uint8_t consecutiveAccelErrors = 0;
bool motionDiagnosticRunning = false;
bool latchProbePending = latchTestEnabled;
bool holdingLatch = false;
bool stayedHigh = true;
UptimeMs holdStartedMs = 0;
UptimeMs nextMotionCheckMs = 0;
uint8_t motionFailures = 0;

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
  Serial0.printf("ESP32 Sleep PoC | T04 BOOT lifecycle | Arduino-ESP32 %s\n", ESP_ARDUINO_VERSION_STR);
  const char *mode = uptimeTestEnabled ? "foundation uptime test" : "normal";
  if (accelTestEnabled) {
    mode = "MPU acceleration test";
  }
  if (latchTestEnabled) mode = "MPU motion latch test";
  if (lifecycleTestEnabled) mode = "BOOT lifecycle test";
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
  if (lifecycleTestEnabled) {
    Serial0.printf("Lifecycle diagnostic | debounce %lu ms | qualified hold %lu ms | countdown %lu ms | reference timing hook %lu ms\n",
                   static_cast<unsigned long>(Config::BUTTON_DEBOUNCE_MS),
                   static_cast<unsigned long>(Config::BUTTON_HOLD_MS),
                   static_cast<unsigned long>(Config::ARMING_DELAY_MS),
                   static_cast<unsigned long>(Config::ZEROING_TIME_MS));
    Serial0.println("Reference validation, tilt, environmental acquisition and light sleep pending | diagnostic ARMED is motion-only when enabled");
  } else {
    Serial0.println("Integrated monitoring, reference validation and light sleep pending | CPU awake");
  }
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

void stopMotion(const char *reason) {
  motionDiagnosticRunning = false;
  const Error cleanup = motionSensor.disableMotion();
  printTimestamp(uptimeMs());
  if (lifecycleTestEnabled) {
    lifecycle.enterFault();
    setStatusLed(false);
  }
  Serial0.printf("MOTION_FAULT | %s | disable/clear: %s | %s | reset after inspection\n",
                 reason, errorText(cleanup), lifecycleTestEnabled ? "FAULT" : "DISARMED");
}

void serviceMotion(UptimeMs nowMs) {
  if (nowMs < nextMotionCheckMs) return;
  const bool high = motionSensor.interruptAsserted();
  if (!holdingLatch && !high) {
    motionFailures = 0;
    return;
  }

  if (latchProbePending) {
    if (!holdingLatch) {
      holdStartedMs = nowMs;
      holdingLatch = true;
      const auto sample = motionSensor.readAccel();
      stayedHigh = motionSensor.interruptAsserted();
      Serial0.printf("LATCH_HOLD | GPIO4 before accel=1 after accel=%u | read=%s\n",
                     stayedHigh, errorText(sample.error));
      if (!sample.valid()) {
        stopMotion(errorText(sample.error));
        return;
      }
    }
    stayedHigh = stayedHigh && high;
    if (nowMs - holdStartedMs < Config::LATCH_TEST_HOLD_MS) return;
    const auto sample = motionSensor.readAccel();
    stayedHigh = stayedHigh && motionSensor.interruptAsserted();
    if (!sample.valid()) {
      stopMotion(errorText(sample.error));
      return;
    }
  }

  const auto event = motionSensor.readAndAcknowledgeMotion(nowMs);
  const bool afterAck = motionSensor.interruptAsserted();
  const bool verified = event.valid() && event.value != MotionSensor::Event::NoMotion;
  if (holdingLatch) {
    Serial0.printf("LATCH_ACK | held %llu ms | GPIO4 high throughout sampled reads=%u | motion bit=%u | after ack=%u | %s\n",
                   nowMs - holdStartedMs, stayedHigh, verified, afterAck, errorText(event.error));
    holdingLatch = latchProbePending = false;
    if (!stayedHigh || !verified) {
      stopMotion("latch probe failed");
      return;
    }
  }
  if (verified) {
    printTimestamp(nowMs);
    Serial0.printf("%s count=%llu suppressed=%llu\n",
                   event.value == MotionSensor::Event::Reported ? "IMPACT_DETECTED" : "IMPACT_SUPPRESSED",
                   motionSensor.reportedCount(), motionSensor.suppressedCount());
  }
  if (!verified || afterAck) {
    printTimestamp(nowMs);
    Serial0.printf("MOTION_CHECK | ack=%s motion bit=%u GPIO4=%u | attempt %u/%u\n",
                   errorText(event.error), verified, afterAck, motionFailures + 1, Config::SENSOR_FAILURE_LIMIT);
    if (++motionFailures >= Config::SENSOR_FAILURE_LIMIT) {
      stopMotion("repeated status or latch failure");
    }
    nextMotionCheckMs = nowMs + Config::MOTION_RETRY_MS;
  } else {
    motionFailures = 0;
  }
}

void serviceLifecycle(UptimeMs nowMs) {
  const bool pressed = bootButton.tick(nowMs, digitalRead(Config::BOOT_PIN) == LOW);
  const bool wasArmed = lifecycle.state() == DeviceController::State::Armed;
  switch (lifecycle.tick(nowMs, pressed, armingBlockReason == nullptr)) {
    case DeviceController::Event::None:
      return;
    case DeviceController::Event::ArmingStarted:
      setStatusLed(true);
      printTimestamp(nowMs);
      Serial0.println("ARMING_STARTED | lifecycle diagnostic");
      return;
    case DeviceController::Event::ReferenceWindowStarted:
      printTimestamp(nowMs);
      Serial0.println("ZEROING_STARTED | diagnostic timing hook; reference validation pending");
      return;
    case DeviceController::Event::ReferenceWindowElapsed: {
      // This opt-in diagnostic exercises the transition without creating a reference.
      printTimestamp(nowMs);
      Serial0.println("ZEROING_SKIPPED | diagnostic timing hook complete");
      if (Config::ENABLE_IMPACT_DETECTION) {
        const Error error = motionSensor.enableMotion();
        if (error != Error::None) {
          stopMotion(errorText(error));
          return;
        }
        motionFailures = 0;
        nextMotionCheckMs = 0;
      }
      lifecycle.completeArm();
      setStatusLed(false);
      printTimestamp(uptimeMs());
      Serial0.printf("ARMED | lifecycle diagnostic | motion %s\n", onOff(Config::ENABLE_IMPACT_DETECTION));
      return;
    }
    case DeviceController::Event::Disarmed:
      setStatusLed(false);
      if (wasArmed && Config::ENABLE_IMPACT_DETECTION) {
        const Error error = motionSensor.disableMotion();
        if (error != Error::None) {
          stopMotion(errorText(error));
          return;
        }
      }
      printTimestamp(uptimeMs());
      Serial0.printf("DISARMED | %s\n", wasArmed ? "lifecycle diagnostic" : "arming cancelled");
      return;
    case DeviceController::Event::ArmRefused:
      printTimestamp(nowMs);
      Serial0.printf("ARMING_REFUSED | FAULT | %s | reset after inspection\n", armingBlockReason);
      return;
  }
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
  if (lifecycleTestEnabled) {
    bootButton = Button(digitalRead(Config::BOOT_PIN) == LOW, nowMs);
    if (!cpuConfigured || !wifiOff || !bluetoothOff) {
      armingBlockReason = "platform startup failed";
    } else if (Config::ENABLE_IMPACT_DETECTION && mpu.error != Error::None) {
      armingBlockReason = errorText(mpu.error);
    }
  }
  nextUptimeLogMs = nowMs + Config::FOUNDATION_UPTIME_LOG_MS;
  nextAccelLogMs = nowMs + Config::MPU_ACCEL_LOG_MS;
  accelDiagnosticRunning = accelTestEnabled && mpu.error == Error::None;
  if (latchTestEnabled && Config::ENABLE_IMPACT_DETECTION && mpu.error == Error::None &&
      cpuConfigured && wifiOff && bluetoothOff) {
    const Error error = motionSensor.enableMotion();
    motionDiagnosticRunning = error == Error::None;
    if (motionDiagnosticRunning) {
      Serial0.printf("Motion diagnostic enabled | INT_ENABLE verified 0x40 | threshold raw %u | duration %u ms | suppression %lu ms\n",
                     Config::MPU_MOTION_THRESHOLD_RAW, Config::IMPACT_DURATION_MS,
                     static_cast<unsigned long>(Config::IMPACT_SUPPRESSION_MS));
    } else {
      stopMotion(errorText(error));
    }
  }
}

void loop() {
  if (lifecycleTestEnabled) {
    serviceLifecycle(uptimeMs());
    if (Config::ENABLE_IMPACT_DETECTION && lifecycle.state() == DeviceController::State::Armed) {
      serviceMotion(uptimeMs());
    }
    yield();
    return;
  }
  if (latchTestEnabled && motionDiagnosticRunning) serviceMotion(uptimeMs());
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
