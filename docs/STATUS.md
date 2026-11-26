# Project status and handoff
Revision 0.4 requirements baseline. Updated for T03 on 2026-11-26.

## Current state
- The foundation initializes UART0, CPU frequency, shared I2C and GPIOs. The firmware starts DISARMED, reports startup faults and stays awake. T02 initializes the MPU6050 for continuous acceleration and reports identity and register readback at startup. T03 configures and verifies the motion detector with its interrupt masked at startup. Only the MotionLatch diagnostic enables it and polls GPIO4 while awake. The optional acceleration diagnostic reports signed XYZ and magnitude once per second.
- Selected architecture: explicit ESP32 light sleep, continuous MPU6050 accelerometer with latched GPIO4 motion interrupt, 1 s tilt checks with 5 s persistence, BME280 four forced samples every 10 s.
- The owner completed the T01 board check. T01 is merged in `78d6897`, the starting base for T02. The owner reported a passing T02 board retest with appropriate acceleration magnitude readings after the initialization-order change.
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
2. Select the board's actual port in Arduino IDE, upload the normal build, and open Serial Monitor at 115200 baud. Power-cycle and capture the T03 banner, `Mode normal`, `WHO_AM_I 0x68`, the readback line and the timestamped `DISARMED` line. Expect matching actual/expected values for `0x6C=0x07`, `0x6B=0x08`, `0x38=0x00`, `0x23=0x00`, `0x6A=0x00`, `0x1A=0x01`, `0x19=0x00`, `0x1C=0x11`, `0x1F=0x32`, `0x20=0x01`, `0x37=0x20`, followed by `MPU6050 ready`. Normal mode should emit the startup report once. Capture the full readback line and any `MPU_INIT_ERROR` if the mismatch recurs.
3. Set `Config::TEST_MODE` in `config.h` to `TestMode::MpuAcceleration`, then upload the diagnostic. Expect `Mode MPU acceleration test` and one timestamped `MPU_ACCEL` line per second. Rest the module flat and stationary and record XYZ and magnitude from several samples. Expect stable magnitude around 1 g. Turn the module upside down and check that the same dominant axis reverses sign. Rest the module on its side and check that the newly vertical axis becomes dominant. Magnitude should return to around 1 g after each move. Record the actual orientation and readings.
4. If feasible, power off, disconnect the MPU module and boot the same diagnostic. Expect `MPU_INIT_ERROR` at the identity read, a `DISARMED` line and no sample output. Power off, reconnect the module and boot again to confirm successful initialization. Restore `Config::TEST_MODE` to `TestMode::None` after testing.

## Task tracker
States: NOT_STARTED, IN_PROGRESS, IMPLEMENTED_PENDING_VERIFICATION, VERIFIED, MERGED, BLOCKED. Do not mark MERGED until actual git history/owner confirms. Hardware evidence must be real. A user-confirmed waiver is separate from a pass.

| ID | State | Build | Hardware | Notes |
|---|---|---|---|---|
| T01 | MERGED | normal and uptime diagnostic passed | owner board check passed | Merge `78d6897` |
| T02 | MERGED | updated normal and both diagnostics passed | owner retest passed | Tip 171f429 present on master. Optional missing-module check unreported. |
| T03 | VERIFIED | raw 40 MotionLatch and raw 50 normal rebuilds passed | V06 and awake V07 passed at raw 50 | Raw 40 sensitivity trial pending. Restore normal mode before owner merge. |
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
- At initial handoff, hardware verification was pending. The startup follow-up below records the first failure and passing retest. T03 depends on the T02 merge.

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
- The original code wrote the filter and range before clearing the reset-default sleep bit. The revised sequence sets gyro standby, wakes on the internal oscillator, and waits 30 ms before programming the filter and range. This tests whether programming while asleep caused the mismatch. Required values and strict readback are retained.
- Re-ran the three compile commands above on Arduino-ESP32 3.3.12. Normal, acceleration diagnostic and uptime diagnostic passed, including the signed conversion assertions. Flash sizes were 393,672, 394,612 and 393,772 bytes respectively. `git diff --check` passed.
- The owner reported that the board tests passed and that `MpuAcceleration` produced appropriate magnitude readings on the revised firmware (`db9aa59` with the diagnostic mode selected). Periodic readings require successful identity and configuration readback in this firmware. Exact XYZ values, the repeated register dump and the optional disconnected-module result were unreported. Restored `TestMode::None` after testing. T02 is verified and ready for owner merge.

### T03 autonomous motion interrupt · 2026-11-26 · `feat/T03-motion-interrupt`

- Started clean at `171f429`, shared by this branch, the owner-verified T02 branch and `master`. No new library dependencies.
- The InvenSense register map rev. 3.2 documents the detector HPF setting, motion threshold at 0x1F, 1 ms/count duration at 0x20, latch control and bit 6 in INT_ENABLE and INT_STATUS. The newer rev. 4.2 map used for T02 omits legacy motion fields. The older product specification describes 1 mg increments while a widely used driver documents 2 mg/LSB. The firmware writes raw 50 and makes no physical threshold estimate for this module. [InvenSense register map rev. 3.2](https://cdck-file-uploads-europe1.s3.dualstack.eu-west-1.amazonaws.com/arduino/original/3X/8/1/81493bebd267609d186184aa85227a5484bfe829.pdf) · [Product specification, motion detection section](https://dfimg.dfrobot.com/enshop/image/data/SEN0142/PS-MPU-6000A.pdf) · [i2cdevlib MPU6050 implementation](https://github.com/jrowberg/i2cdevlib/blob/master/MSP430/MPU6050/MPU6050.cpp)
- `MotionSensor` owns the destructive INT_STATUS read. Enable masks interrupts, discards stale status, verifies the motion-only mask and starts fresh session counts. Disable masks, verifies and clears. Only acknowledged motion status affects counters. Suppression uses elapsed recognition time since the last reported event, with the boundary included for reporting. A value of zero disables suppression.
- The diagnostic polls GPIO4 while awake. The first assertion exercises the latch. Subsequent assertions receive immediate service. Persistent assertion or status failures trigger bounded rechecks and a diagnostic stop with disable/clear result. Reset after checking wiring or sensor faults.

- Arduino CLI `core list` confirmed 3.3.12 after granting access to the installed package directory. All four compiles passed, including the T02 conversion assertions. Flash bytes by mode were 393704 (None), 395852 (MotionLatch), 394644 (MpuAcceleration) and 393820 (FoundationUptime). Compared the diagnostic sources against the tracked sketch with only the mode selection substituted. Only the MotionLatch binary contained the `LATCH_ACK` marker. `git diff --check` passed.

Build commands from the repository root, using the CLI and config paths in the T02 handoff. Each diagnostic uses an ignored copy of the sketch with the corresponding `TEST_MODE`.

```powershell
foreach ($mode in 'None','MotionLatch','MpuAcceleration','FoundationUptime') {
  $sketch = if ($mode -eq 'None') {'ESP32_Sleep_PoC'} else {"build-t03-$mode/ESP32_Sleep_PoC"}
  & $cli compile --config-file $configPath --fqbn esp32:esp32:esp32 --build-path "build-t03-$mode/firmware" $sketch
  if ($LASTEXITCODE) {exit $LASTEXITCODE}
}
```

#### Owner board check (V06 and awake portion of V07)

1. Follow the wiring and normal-mode startup checks above. With power off confirm MPU INT is connected to GPIO4. Leave the module stationary at boot. Select `TestMode::MotionLatch` in `config.h`, upload and monitor at 115200 baud. Confirm the banner says `Mode MPU motion latch test` and capture the `Motion diagnostic enabled | INT_ENABLE verified 0x40` line before tapping. The startup register dump shows 0x38=0x00 in both normal and diagnostic builds because it precedes diagnostic enable. The feature setting `Impact on` indicates configuration only.
2. For the raw 40 trial, verify 0x1F reads back 0x28 and the enable line reports threshold raw 40. Tap once, then keep the module still. Capture `LATCH_HOLD` and `LATCH_ACK`. Expect a successful acceleration read, HIGH before and after it, HIGH at each sampled check over the 250 ms hold, verified motion status and LOW after acknowledgement while stationary. Reset to repeat. A logic analyzer on GPIO4 can check continuity between software samples.
3. Tap about 500 ms apart. Capture timestamped `IMPACT_DETECTED` lines and the counts. Try closer taps and record any suppressed notifications. Ringing can retrigger, and multiple taps can share one latch. Record spacing, mounting and unexpected events. If GPIO4 fails to hold or clear, or motion interrupts never appear, retain the logs and stop at T03 for diagnosis.
4. Restore `TestMode::None` and confirm the normal firmware stays disarmed and quiet when tapped. The first board result is recorded below. A passing latch check and deliberate tap observations gate merge. Sleep wake-up verification belongs to T08.

## Verification evidence index
Add entries keyed V01..V14 with date, firmware commit/config, procedure, expected, observed evidence, pass/fail/pending/waived. Do not store fabricated serial logs as actual observations. V14 remains deferred.

- **V01 build and T01 board observations, 2026-11-25.** Uncommitted T01 on base `3f15c02`. Normal mode and `FoundationUptime` both compiled for ESP32 Dev Module on Arduino-ESP32 3.3.12. The owner observed serial-reported I2C initialization, 80 MHz actual CPU frequency, a timestamped `DISARMED` line and advancing diagnostic uptime. After deliberate reset, the owner observed uptime restart near zero and advance as expected. The T01 board check passed. Sensor identity and missing-sensor arming checks are deferred to their respective implementation tasks.
- **V01 T02 build and MPU board check, 2026-11-25.** Uncommitted T02 on base `78d6897`. All three variants compiled for ESP32 Dev Module on Arduino-ESP32 3.3.12. Identity, register readbacks, approximate 1 g stationary magnitude and rotation/sign are pending board evidence. Missing-MPU startup handling is also pending a board check if feasible. The arming gate is covered by later lifecycle work.
- **V01 T02 first MPU startup, 2026-11-25.** Owner tested the original normal build before the wake-order change. Identity read passed with `0x68`. Configuration readback failed at `0x1A` with actual `0x00` and expected `0x01`. Firmware reported the fault and stayed `DISARMED`.
- **V01 T02 MPU retest, 2026-11-25.** Owner reported passing board tests on the revised firmware with `MpuAcceleration` selected and appropriate magnitude readings. Startup success follows from the diagnostic running. Optional missing-module check unreported.
- **V06 T03 first latch probe, 2026-11-26.** Owner flashed the MotionLatch build on Arduino-ESP32 3.3.12. Identity and all 11 startup register readbacks matched, including INT_PIN_CFG 0x20. Motion-only enable read back 0x40. GPIO4 sampled HIGH before the first acceleration read and LOW immediately afterward. At 250 ms the status transfer succeeded with motion bit clear and GPIO4 LOW. At uptime 00:00:00.660 the diagnostic reported MOTION_FAULT, successfully disabled/cleared the interrupt and stayed DISARMED. The trace establishes neither the source of the initial HIGH nor why it fell. Deliberate tap timing and INT wire continuity were unreported. With power off verify the MPU INT header connects to ESP32 GPIO4 (previous wiring used GPIO17). Reset with the module stationary, note whether the probe starts by itself and, if it waits, tap once. If the failure persists with verified wiring, compare MPU INT and GPIO4 during the probe, preferably alongside I2C. V07 tap counts remain pending.
- **V06 T03 rewired board follow-up, 2026-11-26.** The owner rewired the board and reported matching MPU startup readbacks with no latch messages when tapping. The excerpt ends at DISARMED and omits the mode banner and the motion-enable confirmation. The workspace was set to TestMode::None when checked. The trace is consistent with normal mode. Repeat with the MotionLatch banner and 0x40 enable confirmation before drawing a conclusion about the rewired interrupt path.
- **V06/V07 T03 passing MotionLatch retest, 2026-11-26.** The owner confirmed the preceding no-response build used the wrong mode. After rewiring, the raw 50 diagnostic reported identity 0x68, matching startup readbacks including MOT_THR 0x32, and motion-only enable 0x40. `LATCH_HOLD` showed GPIO4 HIGH before and after a successful acceleration read. `LATCH_ACK` showed GPIO4 HIGH throughout the sampled checks over 250 ms, the motion bit set, and GPIO4 LOW after successful acknowledgement. The log contains 35 consecutive `IMPACT_DETECTED` reports from uptime 00:00:04.197 to 00:05:08.700 with zero suppressed notifications reported. The owner observed that strong taps were needed. V06 and the awake portion of V07 pass at raw 50. Sleep wake remains for T08. The diagnostic-only raw 40 sensitivity trial still awaits board data on lighter taps and stationary behavior.
- **T03 sensitivity trial build.** Arduino CLI passed both builds for `esp32:esp32:esp32` with the same CLI and config as above. The MotionLatch build used the workspace sketch and `--build-path build-t03-MotionLatch/firmware`. The normal build used `build-t03-threshold-normal/ESP32_Sleep_PoC`, a copy with `TestMode::None` selected, and `--build-path build-t03-None/firmware`. Flash usage was 395852 and 393704 bytes respectively. The workspace still selects MotionLatch for the owner retest. Restore None before merge.
