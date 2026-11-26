#include <Arduino.h>
#include <Wire.h>
#include <esp_arduino_version.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <math.h>

#include "DeviceController.h"
#include "EnvironmentSensor.h"
#include "MotionSensor.h"
#include "PowerManager.h"
#include "TiltDetector.h"
#include "Uptime.h"
#include "config.h"

namespace {

constexpr bool statusLedEnabled = Config::ENABLE_STATUS_LED && Config::STATUS_LED_PIN >= 0;
constexpr bool uptimeTestEnabled = Config::TEST_MODE == Config::TestMode::FoundationUptime;
constexpr bool accelTestEnabled = Config::TEST_MODE == Config::TestMode::MpuAcceleration;
constexpr bool latchTestEnabled = Config::TEST_MODE == Config::TestMode::MotionLatch;
constexpr bool environmentTest = Config::TEST_MODE == Config::TestMode::Environment;
constexpr bool normalMode = Config::TEST_MODE == Config::TestMode::None;
constexpr bool lifecycleTest = Config::TEST_MODE == Config::TestMode::ButtonLifecycle;
constexpr bool monitoringEnabled = normalMode || lifecycleTest || environmentTest;
constexpr bool environmentEnabled = (normalMode || environmentTest) && Config::ENABLE_ENV_ACQUISITION;
constexpr bool lightSleepEnabled = (normalMode || environmentTest) && Config::ENABLE_SLEEP;
MotionSensor motionSensor(Wire);
EnvironmentSensor environmentSensor(Wire, uptimeMs);
uint8_t environmentFailures = 0;
DeviceController lifecycle;
PowerManager power;
uint64_t armedAtUs = 0;
StaticSemaphore_t schedulerWakeStorage;
SemaphoreHandle_t schedulerWake = nullptr;
uint8_t lifecycleAccelFailures = 0;
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
                        const MotionSensor::Initialization &mpu,
                        const EnvironmentSensor::Initialization &bme) {
  Serial0.printf("ESP32 Sleep PoC | T08 | Arduino-ESP32 %s | ESP-IDF %s\n",
                 ESP_ARDUINO_VERSION_STR, ESP.getSdkVersion());
  const char *mode = uptimeTestEnabled ? "foundation uptime test" : "normal";
  if (accelTestEnabled) {
    mode = "MPU acceleration test";
  }
  if (latchTestEnabled) mode = "MPU motion latch test";
  if (lifecycleTest) mode = "BOOT lifecycle test";
  if (environmentTest) mode = "BME280 acquisition test";
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
  if (environmentEnabled) {
    if (bme.address != 0) {
      Serial0.printf("BME probe | address 0x%02X | chip ID 0x%02X\n", bme.address, bme.chipId);
    }
    if (bme.error == EnvironmentSensor::Error::None) {
      Serial0.printf("BME280 ready | %u forced samples every %lu ms | T x%u RH x%u P x%u | IIR off\n",
                     Config::ENV_SAMPLE_COUNT, static_cast<unsigned long>(Config::ENV_INTERVAL_MS),
                     Config::ENV_TEMP_OVERSAMPLING, Config::ENV_HUM_OVERSAMPLING,
                     Config::ENV_PRES_OVERSAMPLING);
    } else {
      printTimestamp(uptimeMs());
      Serial0.printf("ENV_INIT_ERROR | %s\n", EnvironmentSensor::errorText(bme.error));
    }
  } else {
    Serial0.println("BME280 acquisition inactive in this build");
  }
  Serial0.printf("Active monitoring | Impact %s | Tilt %s | Environment %s | Sleep %s | Status LED %s\n",
                 onOff((monitoringEnabled || latchTestEnabled) && Config::ENABLE_IMPACT_DETECTION),
                 onOff(monitoringEnabled && Config::ENABLE_TILT_DETECTION),
                 onOff(environmentEnabled), onOff(lightSleepEnabled), onOff(statusLedEnabled));
  if (monitoringEnabled) {
    Serial0.printf("BOOT | debounce %lu ms | qualified hold %lu ms | countdown %lu ms | reference window %lu ms\n",
                   static_cast<unsigned long>(Config::BUTTON_DEBOUNCE_MS),
                   static_cast<unsigned long>(Config::BUTTON_HOLD_MS),
                   static_cast<unsigned long>(Config::ARMING_DELAY_MS),
                   static_cast<unsigned long>(Config::ZEROING_TIME_MS));
    Serial0.printf("Tilt %s | check every %lu ms | detect at %.1f deg | clear at %.1f deg | persistence %lu ms\n",
                   onOff(Config::ENABLE_TILT_DETECTION), static_cast<unsigned long>(Config::TILT_INTERVAL_MS),
                   Config::TILT_THRESHOLD_DEG, Config::TILT_THRESHOLD_DEG - Config::TILT_HYSTERESIS_DEG,
                   static_cast<unsigned long>(Config::TILT_PERSISTENCE_MS));
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

void clearMonitoringOutputs() {
  if (environmentEnabled) environmentSensor.cancel();
  setStatusLed(false);
}

void reportSleepStats() {
  const uint64_t armedUs = static_cast<uint64_t>(esp_timer_get_time()) - armedAtUs;
  const auto &stats = power.stats();
  const uint64_t awakeUs = armedUs > stats.sleepCallUs ? armedUs - stats.sleepCallUs : 0;
  const double utilization = armedUs ? 100.0 * stats.sleepCallUs / armedUs : 0.0;
  printTimestamp(uptimeMs());
  Serial0.printf("SLEEP_STATS | mode %s | sleep calls %.3f ms | awake approx %.3f ms | utilization approx %.1f %%"
                 " | wakes timer=%llu GPIO=%llu other=%llu | GPIO wake observations BOOT=%llu MPU=%llu"
                 " | entry errors=%llu skips=%llu\n",
                 lightSleepEnabled ? "light sleep" : "awake", stats.sleepCallUs / 1000.0, awakeUs / 1000.0,
                 utilization, stats.timerWakes, stats.gpioWakes, stats.otherWakes, stats.bootObserved,
                 stats.motionObserved, stats.entryErrors, stats.skips);
}

void stopMotion(const char *reason) {
  motionDiagnosticRunning = false;
  const Error cleanup = motionSensor.disableMotion();
  printTimestamp(uptimeMs());
  if (monitoringEnabled) {
    lifecycle.enterFault();
    clearMonitoringOutputs();
  }
  Serial0.printf("MOTION_FAULT | %s | disable/clear: %s | %s | reset after inspection\n",
                 reason, errorText(cleanup), monitoringEnabled ? "FAULT" : "DISARMED");
}

void serviceMotionStatus(UptimeMs nowMs) {
  if (nowMs < nextMotionCheckMs) return;
  nextMotionCheckMs = 0;
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

void checkLifecycleAccel(const Result<Vec3> &sample, UptimeMs nowMs, bool reportFirstFailure) {
  if (sample.valid()) {
    lifecycleAccelFailures = 0;
    return;
  }
  ++lifecycleAccelFailures;
  if (reportFirstFailure && lifecycleAccelFailures == 1) {
    printTimestamp(nowMs);
    Serial0.printf("TILT_ACCEL_ERROR | %s | angle unavailable\n", errorText(sample.error));
  }
  if (lifecycleAccelFailures < Config::SENSOR_FAILURE_LIMIT) return;

  const bool wasArmed = lifecycle.state() == DeviceController::State::Armed;
  lifecycle.enterFault();
  clearMonitoringOutputs();
  const bool cleanupNeeded = wasArmed && Config::ENABLE_IMPACT_DETECTION;
  const Error cleanup = cleanupNeeded ? motionSensor.disableMotion() : Error::None;
  printTimestamp(uptimeMs());
  Serial0.printf("MPU_ACCEL_FAULT | %s | motion disable/clear: %s | FAULT | reset after inspection\n",
                 errorText(sample.error), cleanupNeeded ? errorText(cleanup) : "already disabled");
}

const char *referenceFailureText(ReferenceCollector::Failure failure) {
  switch (failure) {
    case ReferenceCollector::Failure::None: return "OK";
    case ReferenceCollector::Failure::InvalidSample: return "invalid acceleration sample";
    case ReferenceCollector::Failure::TooFewSamples: return "too few scheduled samples";
    case ReferenceCollector::Failure::Magnitude: return "reference magnitude out of range";
    case ReferenceCollector::Failure::Movement: return "reference movement exceeded stability limit";
  }
  return "unknown reference failure";
}

void serviceLifecycleEvent(DeviceController::Event event, UptimeMs nowMs, bool wasArmed) {
  switch (event) {
    case DeviceController::Event::None:
      return;
    case DeviceController::Event::ArmingStarted:
      lifecycleAccelFailures = 0;
      setStatusLed(true);
      printTimestamp(nowMs);
      Serial0.println("ARMING_STARTED");
      return;
    case DeviceController::Event::ReferenceWindowStarted:
      lifecycle.reference().start(nowMs);
      printTimestamp(nowMs);
      Serial0.println("ZEROING_STARTED | keep device stationary");
      return;
    case DeviceController::Event::ReferenceWindowElapsed: {
      const auto outcome = lifecycle.reference().finish();
      if (outcome.failure != ReferenceCollector::Failure::None) {
        printTimestamp(nowMs);
        Serial0.printf("ZEROING_RETRY | %s | samples=%lu\n",
                       referenceFailureText(outcome.failure), static_cast<unsigned long>(outcome.samples));
        const UptimeMs retryMs = uptimeMs();
        lifecycle.retryReference(retryMs);
        lifecycle.reference().start(retryMs);
        printTimestamp(retryMs);
        Serial0.println("ZEROING_STARTED | keep device stationary");
        return;
      }
      printTimestamp(nowMs);
      Serial0.printf("ZEROING_OK | samples=%lu | magnitude=%.3f g | RMS residual=%.4f g\n",
                     static_cast<unsigned long>(outcome.samples), outcome.reference.magnitudeG,
                     outcome.rmsResidualG);
      if (Config::ENABLE_IMPACT_DETECTION) {
        const Error error = motionSensor.enableMotion();
        if (error != Error::None) {
          stopMotion(errorText(error));
          return;
        }
        motionFailures = 0;
        nextMotionCheckMs = 0;
      }
      armedAtUs = esp_timer_get_time();
      const UptimeMs armedMs = armedAtUs / 1000;
      power.resetStats();
      environmentFailures = 0;
      lifecycle.completeArm(armedMs, outcome.reference, Config::ENABLE_TILT_DETECTION, environmentEnabled);
      setStatusLed(false);
      printTimestamp(armedMs);
      Serial0.printf("ARMED | motion %s | tilt %s | environment %s | sleep %s\n",
                     onOff(Config::ENABLE_IMPACT_DETECTION), onOff(Config::ENABLE_TILT_DETECTION),
                     onOff(environmentEnabled), onOff(lightSleepEnabled));
      return;
    }
    case DeviceController::Event::Disarmed:
      clearMonitoringOutputs();
      if (wasArmed && Config::ENABLE_IMPACT_DETECTION) {
        const Error error = motionSensor.disableMotion();
        if (error != Error::None) {
          stopMotion(errorText(error));
          return;
        }
      }
      printTimestamp(uptimeMs());
      Serial0.println(wasArmed ? "DISARMED" : "DISARMED | arming cancelled");
      return;
    case DeviceController::Event::ArmRefused:
      printTimestamp(nowMs);
      Serial0.printf("ARMING_REFUSED | FAULT | %s | reset after inspection\n", armingBlockReason);
      return;
  }
}

void reportEnvironment() {
  EnvironmentSensor::Burst result;
  if (!environmentSensor.takeResult(result)) return;
  printTimestamp(uptimeMs());
  if (result.error == EnvironmentSensor::Error::None) {
    environmentFailures = 0;
    Serial0.printf("ENV_REPORT | temperature %.2f C | humidity %.2f %%RH | pressure %.2f hPa | samples %u\n",
                   result.average.temperatureC, result.average.humidityPercent, result.average.pressureHpa,
                   result.samples);
    reportSleepStats();
    return;
  }
  Serial0.printf("ENV_ERROR | %s | consecutive failures %u/%u\n",
                 EnvironmentSensor::errorText(result.error), ++environmentFailures, Config::SENSOR_FAILURE_LIMIT);
  if (environmentFailures < Config::SENSOR_FAILURE_LIMIT) return;

  lifecycle.enterFault();
  clearMonitoringOutputs();
  const bool motionEnabled = Config::ENABLE_IMPACT_DETECTION;
  const Error cleanup = motionEnabled ? motionSensor.disableMotion() : Error::None;
  printTimestamp(uptimeMs());
  Serial0.printf("ENV_FAULT | repeated burst failures | motion disable/clear: %s | FAULT | reset after inspection\n",
                 motionEnabled ? errorText(cleanup) : "already disabled");
}

struct SchedulerIo {
  UptimeMs now() const { return uptimeMs(); }
  bool bootPressed() const { return digitalRead(Config::BOOT_PIN) == LOW; }
  bool motionEnabled() const { return Config::ENABLE_IMPACT_DETECTION; }
  bool motionAsserted() const { return motionSensor.interruptAsserted(); }
  UptimeMs motionRetryAt() const { return nextMotionCheckMs; }
  bool readyToArm() const { return armingBlockReason == nullptr; }
  void serviceMotion(UptimeMs nowMs) { serviceMotionStatus(nowMs); }
  void lifecycleEvent(DeviceController::Event event, UptimeMs nowMs, bool wasArmed) {
    serviceLifecycleEvent(event, nowMs, wasArmed);
  }

  void sampleReference(UptimeMs nowMs) {
    const auto sample = motionSensor.readAccel();
    lifecycle.reference().observe(nowMs, sample);
    checkLifecycleAccel(sample, now(), false);
  }

  void checkTilt(UptimeMs nowMs) {
    const auto sample = motionSensor.readAccel();
    auto &tilt = lifecycle.tilt();
    const uint64_t previousMisses = tilt.missedChecks();
    tiltObservation = tilt.observe(nowMs, sample);
    tiltRecognizedMs = now();
    missedTiltChecks = tilt.missedChecks() - previousMisses;
    checkLifecycleAccel(sample, tiltRecognizedMs, true);
  }

  UptimeMs environmentDeadline() const { return environmentSensor.nextDeadline(); }
  bool startEnvironment(UptimeMs nowMs) {
    if (!environmentSensor.start()) return false;
    if (environmentTest) {
      printTimestamp(nowMs);
      Serial0.println("ENV_BURST_STARTED");
    }
    return true;
  }
  void advanceEnvironment(UptimeMs nowMs) { environmentSensor.tick(nowMs); }
  void environmentMissed(UptimeMs nowMs, UptimeMs slots, UptimeMs total) {
    printTimestamp(nowMs);
    Serial0.printf("ENV_MISSED | skipped %llu slots | session total %llu\n", slots, total);
  }

  void report() {
    if (missedTiltChecks != 0) {
      printTimestamp(tiltRecognizedMs);
      Serial0.printf("TILT_MISSED | skipped %llu checks | session total %llu\n",
                     missedTiltChecks, lifecycle.tilt().missedChecks());
    }
    if (tiltObservation.transition != TiltDetector::Transition::None) {
      printTimestamp(tiltRecognizedMs);
      Serial0.printf("%s angle=%.2f deg\n",
                     tiltObservation.transition == TiltDetector::Transition::Detected ? "TILT_DETECTED" : "TILT_CLEARED",
                     tiltObservation.angleDeg);
    }
    if (environmentEnabled) reportEnvironment();
  }

  TiltDetector::Observation tiltObservation;
  UptimeMs tiltRecognizedMs = 0;
  UptimeMs missedTiltChecks = 0;
};

void ARDUINO_ISR_ATTR signalScheduler() {
  BaseType_t higherPriorityTaskWoken = pdFALSE;
  xSemaphoreGiveFromISR(schedulerWake, &higherPriorityTaskWoken);
  if (higherPriorityTaskWoken == pdTRUE) portYIELD_FROM_ISR();
}

void waitAwake(UptimeMs deadlineMs) {
  const UptimeMs nowMs = uptimeMs();
  if (deadlineMs <= nowMs) return;
  const UptimeMs remainingMs = deadlineMs - nowMs;
  const UptimeMs waitMs = remainingMs < Config::AWAKE_INPUT_POLL_MS ? remainingMs : Config::AWAKE_INPUT_POLL_MS;
  const TickType_t ticks = pdMS_TO_TICKS(waitMs);
  const TickType_t timeout = ticks > 0 ? ticks : 1;
  xSemaphoreTake(schedulerWake, timeout);
}

void idleScheduler(const DeviceController::IdleState &idle) {
  if (!lightSleepEnabled) {
    waitAwake(idle.deadlineMs);
    return;
  }
  if (armingBlockReason != nullptr || lifecycle.state() == DeviceController::State::Fault) {
    waitAwake(idle.deadlineMs);
    return;
  }

  const auto result = power.sleepUntil(idle.deadlineMs, idle.bootWakeEnabled, idle.motionWakeEnabled, idle.bootPressed);
  if (result.action == PowerManager::Action::WaitAwake) {
    waitAwake(std::min(idle.deadlineMs, uptimeMs() + Config::SLEEP_RETRY_MS));
  } else if (result.action == PowerManager::Action::Fault) {
    const bool wasArmed = lifecycle.state() == DeviceController::State::Armed;
    lifecycle.enterFault();
    clearMonitoringOutputs();
    const Error cleanup = wasArmed && Config::ENABLE_IMPACT_DETECTION ? motionSensor.disableMotion() : Error::None;
    printTimestamp(uptimeMs());
    Serial0.printf("POWER_FAULT | %s: %s (0x%X) | motion cleanup %s | FAULT | reset after inspection\n",
                   result.operation, esp_err_to_name(result.error), static_cast<unsigned>(result.error),
                   errorText(cleanup));
    if (wasArmed) reportSleepStats();
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
  const auto bme = wireReady && environmentEnabled ? environmentSensor.begin() : EnvironmentSensor::Initialization{};
  printStartupBanner(cpuConfigured, wireReady, wifiOff, bluetoothOff, mpu, bme);
  const UptimeMs nowMs = uptimeMs();
  if (monitoringEnabled) {
    lifecycle.initializeButton(nowMs, digitalRead(Config::BOOT_PIN) == LOW);
    schedulerWake = xSemaphoreCreateBinaryStatic(&schedulerWakeStorage);
    // GPIO level wake and edge interrupts share the hardware trigger type.
    if (!lightSleepEnabled) {
      attachInterrupt(Config::BOOT_PIN, signalScheduler, CHANGE);
      if (Config::ENABLE_IMPACT_DETECTION) attachInterrupt(Config::MPU_INT_PIN, signalScheduler, RISING);
    }
    if (!cpuConfigured || !wifiOff || !bluetoothOff) {
      armingBlockReason = "platform startup failed";
    } else if (mpu.error != Error::None) {
      armingBlockReason = errorText(mpu.error);
    } else if (environmentEnabled && bme.error != EnvironmentSensor::Error::None) {
      armingBlockReason = "required BME280 unavailable (see ENV_INIT_ERROR)";
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
  if (monitoringEnabled) {
    SchedulerIo io;
    const bool wasArmed = lifecycle.state() == DeviceController::State::Armed;
    const auto idle = lifecycle.run(io);
    if (wasArmed && lifecycle.state() != DeviceController::State::Armed) reportSleepStats();
    idleScheduler(idle);
    return;
  }
  if (latchTestEnabled && motionDiagnosticRunning) serviceMotionStatus(uptimeMs());
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
