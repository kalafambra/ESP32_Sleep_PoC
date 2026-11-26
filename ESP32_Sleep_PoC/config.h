#pragma once

#include <stdint.h>

namespace Config {

constexpr int I2C_SDA_PIN = 21;
constexpr int I2C_SCL_PIN = 22;
constexpr int MPU_INT_PIN = 4;
constexpr uint8_t MPU_ADDRESS = 0x68;
constexpr uint8_t BME_PRIMARY_ADDRESS = 0x76;
constexpr uint8_t BME_SECONDARY_ADDRESS = 0x77;
constexpr int BOOT_PIN = 0;
constexpr int STATUS_LED_PIN = 2;
constexpr int UART_TX_PIN = 1;
constexpr int UART_RX_PIN = 3;
constexpr uint32_t I2C_CLOCK_HZ = 400000;
constexpr uint16_t I2C_TIMEOUT_MS = 50;
constexpr uint32_t MPU_REGISTER_READY_WAIT_MS = 100;
constexpr uint32_t MPU_WAKE_SETTLE_MS = 30;
constexpr uint32_t MPU_ACCEL_SETTLE_MS = 30;
constexpr uint32_t SERIAL_BAUD = 115200;
constexpr uint32_t CPU_FREQ_MHZ = 80;

constexpr uint32_t ENV_INTERVAL_MS = 10000;
constexpr uint8_t ENV_SAMPLE_COUNT = 4;
constexpr uint8_t ENV_TEMP_OVERSAMPLING = 2;
constexpr uint8_t ENV_HUM_OVERSAMPLING = 4;
constexpr uint8_t ENV_PRES_OVERSAMPLING = 16;
constexpr uint32_t ENV_RESET_SETTLE_MS = 5;
constexpr uint32_t ENV_CONVERSION_CHECK_MS = 60;
constexpr uint32_t ENV_CONVERSION_RECHECK_MS = 5;
constexpr uint32_t ENV_CONVERSION_TIMEOUT_MS = 150;

constexpr uint32_t TILT_INTERVAL_MS = 1000;
constexpr float TILT_THRESHOLD_DEG = 30.0f;
constexpr float TILT_HYSTERESIS_DEG = 3.0f;
constexpr uint32_t TILT_PERSISTENCE_MS = 5000;
constexpr uint8_t TILT_FILTER_SAMPLES = 3;
constexpr float TILT_MIN_VALID_RATIO = 0.85f;
constexpr float TILT_MAX_VALID_RATIO = 1.15f;

constexpr uint8_t IMPACT_THRESHOLD_RAW = 50;
constexpr uint8_t IMPACT_DURATION_MS = 1;
constexpr uint32_t IMPACT_SUPPRESSION_MS = 100;

constexpr uint32_t BUTTON_DEBOUNCE_MS = 20;
constexpr uint32_t BUTTON_HOLD_MS = 300;
constexpr uint32_t ARMING_DELAY_MS = 10000;
constexpr uint32_t ZEROING_TIME_MS = 2000;
constexpr uint32_t ZEROING_SAMPLE_MS = 10;
constexpr float ZEROING_MIN_MAGNITUDE_G = 0.85f;
constexpr float ZEROING_MAX_MAGNITUDE_G = 1.15f;
constexpr float ZEROING_MAX_RMS_RESIDUAL_G = 0.03f;
constexpr uint32_t ZEROING_MIN_SAMPLE_PERCENT = 90;
constexpr uint8_t SENSOR_FAILURE_LIMIT = 3;

constexpr bool ENABLE_SLEEP = true;
constexpr bool ENABLE_STATUS_LED = true;
constexpr bool ENABLE_IMPACT_DETECTION = true;
constexpr bool ENABLE_TILT_DETECTION = true;
constexpr bool ENABLE_ENV_ACQUISITION = true;

enum class TestMode { None, FoundationUptime, MpuAcceleration, MotionLatch, ButtonLifecycle, Environment };
constexpr TestMode TEST_MODE = TestMode::Environment;
constexpr uint8_t MOTION_LATCH_TEST_THRESHOLD_RAW = 40;
constexpr uint8_t MPU_MOTION_THRESHOLD_RAW =
    TEST_MODE == TestMode::MotionLatch ? MOTION_LATCH_TEST_THRESHOLD_RAW : IMPACT_THRESHOLD_RAW;
constexpr uint32_t LATCH_TEST_HOLD_MS = 250;
constexpr uint32_t MOTION_RETRY_MS = 20;
static_assert(LATCH_TEST_HOLD_MS > 0 && MOTION_RETRY_MS > 0, "Motion diagnostic intervals must be positive");
constexpr uint32_t FOUNDATION_UPTIME_LOG_MS = 1000;
constexpr uint32_t MPU_ACCEL_LOG_MS = 1000;
constexpr uint32_t AWAKE_IDLE_YIELD_MS = 1;

constexpr bool validOversampling(uint8_t multiplier) {
  return multiplier == 1 || multiplier == 2 || multiplier == 4 || multiplier == 8 || multiplier == 16;
}

static_assert(STATUS_LED_PIN == -1 || STATUS_LED_PIN == 2, "LED pin must be GPIO2 or -1");
static_assert(I2C_CLOCK_HZ == 100000 || I2C_CLOCK_HZ == 400000, "Use 400 kHz or diagnostic 100 kHz I2C");
static_assert(I2C_TIMEOUT_MS > 0 && SERIAL_BAUD > 0, "I2C timeout and baud must be positive");
static_assert(CPU_FREQ_MHZ == 80 || CPU_FREQ_MHZ == 160 || CPU_FREQ_MHZ == 240,
              "Use a supported ESP32 PLL CPU frequency");
static_assert(ENV_SAMPLE_COUNT > 0 && ENV_INTERVAL_MS > 0, "Environmental count and interval must be positive");
static_assert(validOversampling(ENV_TEMP_OVERSAMPLING) && validOversampling(ENV_HUM_OVERSAMPLING)
                  && validOversampling(ENV_PRES_OVERSAMPLING),
              "Use supported BME280 oversampling multipliers");
// Bosch BME280 datasheet section 9.1, maximum conversion time in microseconds.
static_assert(ENV_CONVERSION_CHECK_MS * 1000ULL >
                  2400 + 2300 * (ENV_TEMP_OVERSAMPLING + ENV_HUM_OVERSAMPLING + ENV_PRES_OVERSAMPLING) &&
                  ENV_CONVERSION_CHECK_MS < ENV_CONVERSION_TIMEOUT_MS && ENV_CONVERSION_RECHECK_MS > 0,
              "BME280 completion checks must leave time for the configured oversampling");
static_assert(ENV_INTERVAL_MS >
                  ENV_SAMPLE_COUNT * (ENV_CONVERSION_TIMEOUT_MS + 2ULL * I2C_TIMEOUT_MS),
              "Environmental interval must accommodate a complete burst");
static_assert(TILT_INTERVAL_MS > 0 && TILT_PERSISTENCE_MS > 0, "Tilt interval and persistence must be positive");
static_assert(TILT_THRESHOLD_DEG >= 5.0f && TILT_THRESHOLD_DEG <= 90.0f, "Tilt threshold must be 5 to 90 degrees");
static_assert(TILT_HYSTERESIS_DEG > 0 && TILT_HYSTERESIS_DEG < TILT_THRESHOLD_DEG, "Invalid tilt hysteresis");
static_assert(TILT_FILTER_SAMPLES % 2 == 1, "Tilt median window must be odd");
static_assert(TILT_MIN_VALID_RATIO > 0 && TILT_MIN_VALID_RATIO <= 1.0f && TILT_MAX_VALID_RATIO >= 1.0f
                  && TILT_MIN_VALID_RATIO < TILT_MAX_VALID_RATIO,
              "Tilt magnitude bounds must include the stationary reference");
static_assert(IMPACT_THRESHOLD_RAW > 0 && MOTION_LATCH_TEST_THRESHOLD_RAW > 0 && IMPACT_DURATION_MS > 0,
              "Impact thresholds and duration must be positive");
static_assert(BUTTON_DEBOUNCE_MS > 0 && BUTTON_HOLD_MS >= BUTTON_DEBOUNCE_MS, "Invalid button timing");
static_assert(ZEROING_TIME_MS > 0 && ZEROING_TIME_MS <= ARMING_DELAY_MS && ZEROING_SAMPLE_MS > 0
                  && ZEROING_SAMPLE_MS <= ZEROING_TIME_MS && ZEROING_TIME_MS % ZEROING_SAMPLE_MS == 0,
              "Zeroing samples must fit in the arming delay");
static_assert(ZEROING_MIN_MAGNITUDE_G > 0 && ZEROING_MIN_MAGNITUDE_G < ZEROING_MAX_MAGNITUDE_G &&
                  ZEROING_MAX_RMS_RESIDUAL_G > 0 && ZEROING_MIN_SAMPLE_PERCENT > 0 &&
                  ZEROING_MIN_SAMPLE_PERCENT <= 100,
              "Invalid zeroing stability limits");
static_assert(SENSOR_FAILURE_LIMIT > 0, "Sensor failure limit must be positive");
static_assert(FOUNDATION_UPTIME_LOG_MS > 0 && AWAKE_IDLE_YIELD_MS > 0, "Foundation intervals must be positive");
static_assert(MPU_REGISTER_READY_WAIT_MS > 0 && MPU_WAKE_SETTLE_MS > 0 &&
                  MPU_ACCEL_SETTLE_MS > 0 && MPU_ACCEL_LOG_MS > 0,
              "MPU intervals must be positive");

}  // namespace Config
