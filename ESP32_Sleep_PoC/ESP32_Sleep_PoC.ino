#include <Arduino.h>
#include <Wire.h>
#include <esp_arduino_version.h>
#include <esp_wifi.h>

#include "Uptime.h"
#include "config.h"

namespace {

constexpr bool statusLedEnabled = Config::ENABLE_STATUS_LED && Config::STATUS_LED_PIN >= 0;
constexpr bool uptimeTestEnabled = Config::TEST_MODE == Config::TestMode::FoundationUptime;
UptimeMs nextUptimeLogMs = 0;

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

void printStartupBanner(bool cpuConfigured, bool wireReady, bool wifiOff, bool bluetoothOff) {
  Serial0.printf("ESP32 Sleep PoC | T01 foundation | Arduino-ESP32 %s\n", ESP_ARDUINO_VERSION_STR);
  Serial0.printf("Mode %s | CPU target %lu MHz, actual %lu MHz | UART %lu baud (TX GPIO%d, RX GPIO%d)\n",
                 uptimeTestEnabled ? "foundation uptime test" : "normal",
                 static_cast<unsigned long>(Config::CPU_FREQ_MHZ), static_cast<unsigned long>(getCpuFrequencyMhz()),
                 static_cast<unsigned long>(Config::SERIAL_BAUD), Config::UART_TX_PIN, Config::UART_RX_PIN);
  Serial0.printf("I2C SDA GPIO%d, SCL GPIO%d, target %lu Hz, timeout %u ms | %s\n",
                 Config::I2C_SDA_PIN, Config::I2C_SCL_PIN, static_cast<unsigned long>(Config::I2C_CLOCK_HZ),
                 static_cast<unsigned>(Config::I2C_TIMEOUT_MS), wireReady ? "initialized" : "initialization failed");
  Serial0.printf("BOOT GPIO%d active LOW | LED GPIO%d active HIGH (%s) | MPU INT GPIO%d active HIGH\n",
                 Config::BOOT_PIN, Config::STATUS_LED_PIN, onOff(statusLedEnabled), Config::MPU_INT_PIN);
  Serial0.printf("Sensor identity checks pending | MPU6050 expected at 0x%02X | BME280 expected at 0x%02X or 0x%02X\n",
                 static_cast<unsigned>(Config::MPU_ADDRESS), static_cast<unsigned>(Config::BME_PRIMARY_ADDRESS),
                 static_cast<unsigned>(Config::BME_SECONDARY_ADDRESS));
  Serial0.printf("Feature settings | Impact %s | Tilt %s | Environment %s | Sleep %s | Status LED %s\n",
                 onOff(Config::ENABLE_IMPACT_DETECTION), onOff(Config::ENABLE_TILT_DETECTION),
                 onOff(Config::ENABLE_ENV_ACQUISITION), onOff(Config::ENABLE_SLEEP), onOff(statusLedEnabled));
  Serial0.println("Foundation operation | Monitoring and light sleep unavailable | CPU awake");
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
  printStartupBanner(cpuConfigured, wireReady, wifiOff, bluetoothOff);
  nextUptimeLogMs = uptimeMs() + Config::FOUNDATION_UPTIME_LOG_MS;
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
  delay(Config::AWAKE_IDLE_YIELD_MS);
}
