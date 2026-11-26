# ESP32 Sleep Mode PoC — wersja polska

## Podłączenie

Podłącz moduły MPU6050 i BME280 do zasilania **3,3 V** i masy **GND** na płytce ESP32-WROOM-32D.

| Połączenie | Pin ESP32 |
| --- | --- |
| Wspólna magistrala I²C, SDA / SCL | GPIO21 / GPIO22 |
| INT modułu MPU6050 (aktywny stan wysoki) | GPIO4 |
| Wbudowany w ESP32 przycisk BOOT (aktywny stan niski) | GPIO0 |
| Wbudowana dioda LED stanu (aktywny stan wysoki) | GPIO2 |
| Port szeregowy TX / RX (115 200 bodów) | GPIO1 / GPIO3 |

W zamontowanym module MPU6050 pozostaw pin AD0 niepodłączony, ponieważ moduł ma rezystor podciągający AD0 do masy. Jeśli moduł zamienny nie ma takiego rezystora, połącz AD0 z GND.

## Obsługa

Po włączeniu zasilania lub resecie urządzenie jest rozbrojone. Połóż je płasko na stole, przytrzymaj przycisk **BOOT** przez co najmniej pół sekundy, a następnie go puść. Nie poruszaj płytką podczas 10-sekundowego odliczania do uzbrojenia, aby urządzenie mogło ustalić położenie odniesienia. Ponownie przytrzymaj **BOOT**, aby anulować uzbrajanie lub rozbroić urządzenie.

Po uzbrojeniu urządzenie zgłasza gwałtowne ruchy, takie jak stuknięcia. Próg wykrywania jest dość wysoki i wymaga mocniejszego stuknięcia całą dłonią w stół. Urządzenie wykrywa również przechylenie o co najmniej 30° względem położenia przy uzbrojeniu, jeśli utrzymuje się ono przez co najmniej 5 sekund. Co 10 sekund przez port szeregowy przesyła temperaturę, wilgotność i ciśnienie. W przerwach między zadaniami ESP32 przechodzi w tryb lekkiego uśpienia.

Co 10 sekund urządzenie podaje również statystyki uśpienia i informacje o źródłach wybudzenia na pinach GPIO.

---

# ESP32 Sleep Mode PoC

## Wiring

Connect the MPU6050 and BME280 to the ESP32-WROOM-32D board’s **3.3 V** and **GND**.

| Connection | ESP32 pin |
| --- | --- |
| Shared I²C SDA / SCL | GPIO21 / GPIO22 |
| MPU6050 INT (active high) | GPIO4 |
| Built-in BOOT button (active low) | GPIO0 |
| Built-in status LED (active high) | GPIO2 |
| Serial TX / RX (115200 baud) | GPIO1 / GPIO3 |

Leave AD0 unconnected on the installed MPU6050 breakout, which has an onboard pull-down. On a replacement breakout without a pull-down, connect AD0 to GND.

## Operation

Power-up and reset leave the device disarmed. Set it level (flat on a table), hold **BOOT** for at least half a second, then release. Keep the board still during the 10-second arming countdown so it can capture its reference orientation. Repeat the hold to cancel arming or disarm.

While armed, the device reports sudden motion such as taps (threshold is relatively high and requires stronger full-hand tap) and detects tilt of 30° or more sustained for at least 5 seconds relative to the arming orientation. It reports temperature, humidity, and pressure every 10 seconds over serial. The ESP32 uses light sleep when idle.

Every 10 seconds, sleep usage statistics and GPIO wake sources are reported.
