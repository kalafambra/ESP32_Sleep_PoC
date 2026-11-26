# ESP32 Sleep Mode PoC

## Wiring

Connect the MPU6050 and BME280 to the ESP32-WROOM-32D board’s **3.3 V** and **GND**.

| Connection | ESP32 pin |
| --- | --- |
| Shared I²C SDA / SCL | GPIO21 / GPIO22 |
| MPU6050 INT (active high) | GPIO4 |
| BOOT button (active low) | GPIO0 |
| Optional status LED (active high) | GPIO2 |
| Serial TX / RX (115200 baud) | GPIO1 / GPIO3 |

Leave AD0 unconnected on the installed MPU6050 breakout, which has an onboard pull-down. On a replacement breakout without a pull-down, connect AD0 to GND.

## Operation

Power-up and reset leave the device disarmed. Set it level (flat on a table), hold **BOOT** for at least half a second, then release. Keep the board still during the 10-second arming countdown so it can capture its reference orientation. Repeat the hold to cancel arming or disarm.

While armed, the device reports sudden motion such as taps (threshold is relatively high and requires stronger full-hand tap) and detects tilt of 30° or more sustained for at least 5 seconds relative to the arming orientation. It reports temperature, humidity, and pressure every 10 seconds over serial. The ESP32 uses light sleep when idle.

Every 10 seconds, sleep usage statistics and GPIO wake sources are reported.
