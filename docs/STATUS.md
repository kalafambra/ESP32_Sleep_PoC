# Project status and handoff
Revision 0.4 requirements baseline. Updated for T01 on 2026-11-25.

## Current state
- T01 foundation initializes UART0, the configured CPU frequency, shared I2C and GPIOs. It starts DISARMED, reports startup failures and stays awake. Sensor identity checks, monitoring, arming and light sleep are deferred to their respective tasks.
- Selected architecture: explicit ESP32 light sleep, continuous MPU6050 accelerometer with latched GPIO4 motion interrupt, 1 s tilt checks with 5 s persistence, BME280 four forced samples every 10 s.
- The owner completed the T01 board check and confirmed that uptime restarted near zero after reset and continued advancing. T01 is verified and ready for owner merge. T02 depends on that merge.
- Hardware wiring change required relative to original: move MPU INT GPIO17 -> GPIO4. Owner approved; physical connection not confirmed.

## Environment
| Item | Recorded value |
|---|---|
| Arduino IDE version | 2.3.10 |
| Arduino CLI version/availability | IDE-bundled Arduino CLI 1.5.1 |
| Arduino-ESP32 core version | `esp32:esp32@3.3.12` |
| Board selection/FQBN | ESP32 Dev Module (`esp32:esp32:esp32`) for ESP32-WROOM-32D. Exact carrier model awaits owner confirmation. |
| OS / toolchain | Windows 25H2 (build 26200), Xtensa ESP GCC package `esp-x32@2601` |
| Dependency versions | `Wire` and ESP-IDF APIs bundled with Arduino-ESP32 3.3.12. No separate libraries for T01. |
| Compile command | See [Build and board checks](#build-and-board-checks). |
| Host test command | T01 has no host test target. |
| Upload/serial procedure | See [Build and board checks](#build-and-board-checks). Owner operates hardware. |
| Sketch path | `ESP32_Sleep_PoC/ESP32_Sleep_PoC.ino` |

## Build and board checks

For a fresh CLI installation, configure Espressif's board package index, install the pinned core and compile from the repository root.

```powershell
arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core install esp32:esp32@3.3.12 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli compile --fqbn esp32:esp32:esp32 --build-path build ESP32_Sleep_PoC
```

The Arduino IDE equivalent is to install `esp32` by Espressif Systems version 3.3.12 in Boards Manager, open `ESP32_Sleep_PoC/ESP32_Sleep_PoC.ino`, select **ESP32 Dev Module**, and click **Verify**. The sketch requests 80 MHz at runtime and prints the actual CPU frequency.

1. With power disconnected, check 3.3 V and common ground for the sensors, SDA on GPIO21, SCL on GPIO22, and MPU INT on GPIO4. Keep BOOT released through reset.
2. Select the board's actual port in Arduino IDE, upload the normal build, and open Serial Monitor at 115200 baud. Reset with BOOT released. Expect a T01 foundation banner showing `Mode normal`, 80 MHz actual CPU frequency, successful 400 kHz-target I2C initialization, pending sensor identity checks, both radios off and a timestamped `DISARMED` line. The LED should stay off when fitted and enabled. Capture the banner and any `STARTUP_FAULT`.
3. Set `Config::TEST_MODE` in `config.h` to `TestMode::FoundationUptime` and upload the diagnostic build. After reset, expect `Mode foundation uptime test` and one timestamped `UPTIME DISARMED` line per second. Watch for at least one minute and record whether uptime advances without resetting or a repeated boot banner. Reset deliberately once and confirm a new banner, uptime starting near zero and `DISARMED` again.
4. Restore `Config::TEST_MODE` to `TestMode::None`. Share the serial evidence and board selection or explicitly waive the T01 hardware gate.

## Task tracker
States: NOT_STARTED, IN_PROGRESS, IMPLEMENTED_PENDING_VERIFICATION, VERIFIED, MERGED, BLOCKED. Do not mark MERGED until actual git history/owner confirms. Hardware evidence must be real. A user-confirmed waiver is separate from a pass.

| ID | State | Build | Hardware | Notes |
|---|---|---|---|---|
| T01 | VERIFIED | normal and uptime diagnostic passed | owner board check passed | Ready for owner merge |
| T02 | NOT_STARTED | pending | pending | |
| T03 | NOT_STARTED | pending | pending | critical motion latch risk |
| T04 | NOT_STARTED | pending | pending | |
| T05 | NOT_STARTED | pending | pending | |
| T06 | NOT_STARTED | pending | pending | |
| T07 | NOT_STARTED | pending | pending | |
| T08 | NOT_STARTED | pending | pending | |
| T09 | NOT_STARTED | pending | pending | |
| T10 | NOT_STARTED | pending | pending | |

## Approved decisions / open risks
- 10 s environmental start-to-start interval; 1 s tilt checks, 5 s validation/persistence; prior 1 s tilt latency superseded. Interpretation: persistence begins at first filtered qualifying check, typical physical step 6–7 s. Owner has not specified hard 5 s end-to-end deadline.
- 30° threshold, any gravity-relative inclination excluding yaw; 3° hysteresis and three-sample median retained from prior proposed design.
- Motion threshold raw 50, duration 1 ms, 100 ms suppression, stability bounds, fault retry count and timing tolerances are initial engineering defaults pending experiments. Verify MPU register semantics on actual module.
- No fixed tap detection acceptance ratio; manual testing at ~500 ms intervals. No measured current yet; exact current validation deferred.
- Carrier board model and sensor behavior still await board verification. No microphone/radios/deep sleep/ULP in baseline.

## Handoff log (append one concise entry per task or substantive fix)
Use this template; replace placeholders with facts and keep previous entries.

```text
Task / date / branch:
Starting base / dependencies:
Implemented changes and interface decisions:
Build/test commands and actual results:
Manual hardware checks requested and actual owner evidence (or pending):
Known limitations / blockers / deviations:
Requirement or task updates (with owner approval if normative):
Merge readiness / commit or merge reference if known:
Next task dependencies / handoff:
```

### T01 foundation · 2026-11-25 · `feat/T01-foundation`

- Starting base was `3f15c02` with a clean working tree. T01 has no task dependencies.
- Added the Arduino sketch, central `Config` defaults, `Vec3`, `Result<T>`, 64-bit millisecond uptime and timestamp formatting. The sketch initializes Wire with a 50 ms I2C timeout, explicitly requests radio shutdown and 80 MHz, and keeps the optional LED off. The default mode prints the banner once. `TestMode::FoundationUptime` adds a one-second uptime line. Added ignore rules for local settings and builds.
- Both Arduino CLI compiles succeeded using the installed core outside the sandbox. The initial in-sandbox attempt hit access restrictions on the installed Arduino package directory. The normal build used 392,352 bytes of flash and 37,348 bytes of global RAM. The uptime diagnostic used 392,476 bytes of flash and 37,356 bytes of global RAM. The diagnostic compile used an ignored copy of the sketch with only `TEST_MODE` changed. The original sketch retains `TestMode::None`.
- A binary string check found `UPTIME DISARMED` in the diagnostic firmware and confirmed its absence in the normal firmware. `git diff --check` passed, and Git ignored both build outputs.
- The owner reported that the serial banner showed I2C initialized and 80 MHz actual CPU frequency and that the normal build printed one timestamped `DISARMED` line. The owner confirmed the hardware check was complete. After a deliberate reset, diagnostic uptime restarted near zero and continued to advance in one-second steps at the displayed millisecond resolution.
- The read-only T01 review found no blocking firmware issues. T01 is ready for owner merge. T02 depends on the merge.

Exact Windows compile commands used from the repository root:

```powershell
& 'D:/ArduinoIDE/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe' compile --config-file C:/Users/przemek/.arduinoIDE/arduino-cli.yaml --fqbn esp32:esp32:esp32 --build-path D:/code/esp32_sleep_poc/build D:/code/esp32_sleep_poc/ESP32_Sleep_PoC
& 'D:/ArduinoIDE/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe' compile --config-file C:/Users/przemek/.arduinoIDE/arduino-cli.yaml --fqbn esp32:esp32:esp32 --build-path D:/code/esp32_sleep_poc/build-test/firmware D:/code/esp32_sleep_poc/build-test/ESP32_Sleep_PoC
```

## Verification evidence index
Add entries keyed V01..V14 with date, firmware commit/config, procedure, expected, observed evidence, pass/fail/pending/waived. Do not store fabricated serial logs as actual observations. V14 remains deferred.

- **V01 build and T01 board observations, 2026-11-25.** Uncommitted T01 on base `3f15c02`. Normal mode and `FoundationUptime` both compiled for ESP32 Dev Module on Arduino-ESP32 3.3.12. The owner observed serial-reported I2C initialization, 80 MHz actual CPU frequency, a timestamped `DISARMED` line and advancing diagnostic uptime. After deliberate reset, the owner observed uptime restart near zero and advance as expected. The T01 board check passed. Sensor identity and missing-sensor arming checks are deferred to their respective implementation tasks.
