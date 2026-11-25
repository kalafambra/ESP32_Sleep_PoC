# Project status and handoff
Revision 0.4 requirements baseline. Updated for T02 on 2026-11-25.

## Current state
- The foundation initializes UART0, CPU frequency, shared I2C and GPIOs. The firmware starts DISARMED, reports startup faults and stays awake. T02 initializes the MPU6050 for continuous acceleration and reports identity and register readback at startup. The optional acceleration diagnostic reports signed XYZ and magnitude once per second.
- Selected architecture: explicit ESP32 light sleep, continuous MPU6050 accelerometer with latched GPIO4 motion interrupt, 1 s tilt checks with 5 s persistence, BME280 four forced samples every 10 s.
- The owner completed the T01 board check. T01 is merged in `78d6897`, the starting base for T02. The first T02 board boot read the expected MPU identity but failed filter configuration readback. The revised initialization order needs a board retest.
- Hardware wiring change required relative to original: move MPU INT GPIO17 -> GPIO4. Owner approved; physical connection not confirmed.

## Environment
| Item | Recorded value |
|---|---|
| Arduino IDE version | 2.3.10 |
| Arduino CLI version/availability | IDE-bundled Arduino CLI 1.5.1 |
| Arduino-ESP32 core version | `esp32:esp32@3.3.12` |
| Board selection/FQBN | ESP32 Dev Module (`esp32:esp32:esp32`) for ESP32-WROOM-32D. Exact carrier model awaits owner confirmation. |
| OS / toolchain | Windows 25H2 (build 26200), Xtensa ESP GCC package `esp-x32@2601` |
| Dependency versions | `Wire` and ESP-IDF APIs bundled with Arduino-ESP32 3.3.12. No separate sensor library for T02. |
| Compile command | See [Build and board checks](#build-and-board-checks). |
| Test command | The Arduino compile runs the T02 signed-conversion `static_assert` checks. |
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

1. With power disconnected, check 3.3 V, common ground, SDA GPIO21, SCL GPIO22, MPU AD0 to GND and MPU INT GPIO4. Keep BOOT released through reset.
2. Select the board's actual port in Arduino IDE, upload the normal build, and open Serial Monitor at 115200 baud. Power-cycle and capture the T02 banner, `Mode normal`, `WHO_AM_I 0x68`, the readback line and the timestamped `DISARMED` line. Expect matching actual/expected values for `0x6C=0x07`, `0x6B=0x08`, `0x38=0x00`, `0x23=0x00`, `0x6A=0x00`, `0x1A=0x01`, `0x19=0x00`, `0x1C=0x10`, followed by `MPU6050 ready`. Normal mode should emit the startup report once. Capture the full readback line and any `MPU_INIT_ERROR` if the mismatch recurs.
3. Set `Config::TEST_MODE` in `config.h` to `TestMode::MpuAcceleration`, then upload the diagnostic. Expect `Mode MPU acceleration test` and one timestamped `MPU_ACCEL` line per second. Rest the module flat and stationary and record XYZ and magnitude from several samples. Expect stable magnitude around 1 g. Turn the module upside down and check that the same dominant axis reverses sign. Rest the module on its side and check that the newly vertical axis becomes dominant. Magnitude should return to around 1 g after each move. Record the actual orientation and readings.
4. If feasible, power off, disconnect the MPU module and boot the same diagnostic. Expect `MPU_INIT_ERROR` at the identity read, a `DISARMED` line and no sample output. Power off, reconnect the module and boot again to confirm successful initialization. Restore `Config::TEST_MODE` to `TestMode::None` after testing.

## Task tracker
States: NOT_STARTED, IN_PROGRESS, IMPLEMENTED_PENDING_VERIFICATION, VERIFIED, MERGED, BLOCKED. Do not mark MERGED until actual git history/owner confirms. Hardware evidence must be real. A user-confirmed waiver is separate from a pass.

| ID | State | Build | Hardware | Notes |
|---|---|---|---|---|
| T01 | MERGED | normal and uptime diagnostic passed | owner board check passed | Merge `78d6897` |
| T02 | IMPLEMENTED_PENDING_VERIFICATION | updated normal and both diagnostics passed | filter readback failed on first boot, retest pending | Retest startup, then check sign/scale and missing module if feasible |
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

### T02 continuous acceleration · 2026-11-25 · `feat/T02-mpu-accel`

- Started from clean base `78d6897`, which includes the merged and board-verified T01. Arduino CLI Core List confirmed Arduino-ESP32 3.3.12.
- Added `MotionSensor::begin()` and `readAccel()`. Initialization checks `WHO_AM_I` before writing, resets the device once, and verifies eight configuration registers. A successful acceleration sample is a single six-byte repeated-start burst scaled to g at 4096 counts/g. A failed or short transfer returns an explicit error and never produces a valid vector. T02 sets up continuous acceleration and leaves motion HPF and interrupt configuration for T03.
- Startup uses 100 ms guards before the identity probe and after reset, plus a 30 ms acceleration settling guard after wake. I2C uses the foundation's 50 ms Wire timeout. The optional one-second diagnostic rejects failed samples, reports consecutive failures and stops after the configured limit of three until reset. Initialization failure emits `MPU_INIT_ERROR` and stays disarmed. [InvenSense register map RM-MPU-6000A-00 rev. 4.2](https://invensense.tdk.com/wp-content/uploads/2015/02/MPU-6000-Register-Map.pdf) [InvenSense product specification PS-MPU-6000A-00 rev. 3.4](https://invensense.tdk.com/wp-content/uploads/2015/02/MPU-6000-Datasheet.pdf)
- The initial sandboxed compile could not access the installed Arduino package directory. The commands below use the installed core with approved access. The two diagnostic sketch copies are ignored and differ from the repository sketch only in the `TEST_MODE` selection. The tracked setting is `TestMode::None`.
- All three builds passed for `esp32:esp32:esp32`. Normal used 393,652 bytes of flash and 37,356 bytes of global RAM. `MpuAcceleration` used 394,592 bytes of flash and 37,372 bytes of global RAM. `FoundationUptime` used 393,752 bytes of flash and 37,364 bytes of global RAM. Compile-time conversion checks passed for zero, ±1 g, −1 count and both signed endpoints. Firmware string checks confirmed the MPU startup marker in all three binaries and each periodic diagnostic marker only in its selected variant. `git diff --check` passed.
- Hardware evidence is pending. Use [Build and board checks](#build-and-board-checks) to capture the actual identity and registers, stationary magnitude, rotation and axis signs, and missing-device behavior if feasible. The T02 merge gate remains pending owner verification. T03 depends on the T02 merge.

```powershell
$cli = 'D:/ArduinoIDE/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe'
$configPath = 'C:/Users/przemek/.arduinoIDE/arduino-cli.yaml'
$repo = 'D:/code/esp32_sleep_poc'
& $cli core list --config-file $configPath
& $cli compile --config-file $configPath --fqbn esp32:esp32:esp32 --build-path "$repo/build" "$repo/ESP32_Sleep_PoC"
foreach ($mode in 'accel', 'uptime') {
  $dir = "$repo/build-t02-$mode"
  & $cli compile --config-file $configPath --fqbn esp32:esp32:esp32 --build-path "$dir/firmware" "$dir/ESP32_Sleep_PoC"
}
```

### T02 startup readback follow-up · 2026-11-25

- The owner reported `WHO_AM_I 0x68` and matching readbacks through `0x6C=0x07`. The filter register `0x1A` returned `0x00` with `0x01` expected. Startup reported `MPU_INIT_ERROR` and `DISARMED`. The later registers in the original sequence were not read after the mismatch.
- The original code wrote the filter and range before clearing the reset-default sleep bit. The revised sequence sets gyro standby, wakes on the internal oscillator, and waits 30 ms before programming the filter and range. This tests whether programming while asleep caused the mismatch. Required values and strict readback are retained. The board retest remains the merge gate.
- Re-ran the three compile commands above on Arduino-ESP32 3.3.12. Normal, acceleration diagnostic and uptime diagnostic passed, including the signed conversion assertions. Flash sizes were 393,672, 394,612 and 393,772 bytes respectively. `git diff --check` passed.

## Verification evidence index
Add entries keyed V01..V14 with date, firmware commit/config, procedure, expected, observed evidence, pass/fail/pending/waived. Do not store fabricated serial logs as actual observations. V14 remains deferred.

- **V01 build and T01 board observations, 2026-11-25.** Uncommitted T01 on base `3f15c02`. Normal mode and `FoundationUptime` both compiled for ESP32 Dev Module on Arduino-ESP32 3.3.12. The owner observed serial-reported I2C initialization, 80 MHz actual CPU frequency, a timestamped `DISARMED` line and advancing diagnostic uptime. After deliberate reset, the owner observed uptime restart near zero and advance as expected. The T01 board check passed. Sensor identity and missing-sensor arming checks are deferred to their respective implementation tasks.
- **V01 T02 build and MPU board check, 2026-11-25.** Uncommitted T02 on base `78d6897`. All three variants compiled for ESP32 Dev Module on Arduino-ESP32 3.3.12. Identity, register readbacks, approximate 1 g stationary magnitude and rotation/sign are pending board evidence. Missing-MPU startup handling is also pending a board check if feasible. The arming gate is covered by later lifecycle work.
- **V01 T02 first MPU startup, 2026-11-25.** Owner tested the original normal build before the wake-order change. Identity read passed with `0x68`. Configuration readback failed at `0x1A` with actual `0x00` and expected `0x01`. Firmware reported the fault and stayed `DISARMED`. The revised startup, stationary and rotation checks, and missing-module check are pending board evidence.
