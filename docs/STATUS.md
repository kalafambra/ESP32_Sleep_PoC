# Project status and handoff
Revision 0.5 requirements baseline. Updated for T06 on 2026-11-26.

## Current state
- The foundation initializes UART0, CPU frequency, shared I2C and GPIOs. The firmware starts DISARMED, reports startup faults and stays awake. T02 initializes the MPU6050 for continuous acceleration and reports identity and register readback at startup. T03 configures and verifies the motion detector with its interrupt masked at startup. The MotionLatch diagnostic enables it at startup. The opt-in T04 ButtonLifecycle diagnostic enables it only on diagnostic arming and disables and clears it on disarm. The optional acceleration diagnostic reports signed XYZ and magnitude once per second.
- Selected architecture: explicit ESP32 light sleep, continuous MPU6050 accelerometer with latched GPIO4 motion interrupt, 1 s tilt checks with 5 s persistence, BME280 four forced samples every 10 s.
- The owner completed the T01 board check. T01 is merged in `78d6897`, the starting base for T02. The owner reported a passing T02 board retest with appropriate acceleration magnitude readings after the initialization-order change.
- The owner verified latched motion on GPIO4 after rewiring for T03.
- The opt-in ButtonLifecycle diagnostic now collects a stationary gravity reference and runs scheduled tilt checks alongside motion. Default mode starts DISARMED and stays awake.
- The opt-in Environment diagnostic runs four-sample BME280 bursts on a 10 s schedule alongside motion and tilt. Select `TestMode::Environment` to exercise it.

## Environment
| Item | Recorded value |
|---|---|
| Arduino IDE version | 2.3.10 |
| Arduino CLI version/availability | IDE-bundled Arduino CLI 1.5.1 |
| Arduino-ESP32 core version | `esp32:esp32@3.3.12` |
| Board selection/FQBN | ESP32 Dev Module (`esp32:esp32:esp32`) for ESP32-WROOM-32D. Exact carrier model awaits owner confirmation. |
| OS / toolchain | Windows 25H2 (build 26200), Xtensa ESP GCC package `esp-x32@2601` |
| Dependency versions | `Wire` and ESP-IDF APIs bundled with Arduino-ESP32 3.3.12. The MPU6050 and BME280 drivers use register I/O and require no separate sensor libraries. |
| Compile command | See [Build and board checks](#build-and-board-checks). |
| Test command | Arduino builds evaluate the T02 signed-conversion assertions. See the T05 and T06 handoffs for fake-time lifecycle, tilt and BME280 checks. |
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

1. With power disconnected, check 3.3 V, common ground, SDA GPIO21, SCL GPIO22, and MPU INT GPIO4. Leave the MPU AD0 header unconnected on the installed breakout with its onboard 4.7 kΩ pull-down. For replacement breakouts, follow HW-03. Keep BOOT released through reset.
2. Select the board's actual port in Arduino IDE, upload the normal build, and open Serial Monitor at 115200 baud. Power-cycle and capture the startup banner, `Mode normal`, `WHO_AM_I 0x68`, the readback line and the timestamped `DISARMED` line. Expect matching actual/expected values for `0x6C=0x07`, `0x6B=0x08`, `0x38=0x00`, `0x23=0x00`, `0x6A=0x00`, `0x1A=0x01`, `0x19=0x00`, `0x1C=0x11`, `0x1F=0x32`, `0x20=0x01`, `0x37=0x20`, followed by `MPU6050 ready`. Normal mode should emit the startup report once. Capture the full readback line and any `MPU_INIT_ERROR` if the mismatch recurs.
   For V01, capture `MPU address 0x68` from the same line as `WHO_AM_I 0x68`.
3. Set `Config::TEST_MODE` in `config.h` to `TestMode::MpuAcceleration`, then upload the diagnostic. Expect `Mode MPU acceleration test` and one timestamped `MPU_ACCEL` line per second. Rest the module flat and stationary and record XYZ and magnitude from several samples. Expect stable magnitude around 1 g. Turn the module upside down and check that the same dominant axis reverses sign. Rest the module on its side and check that the newly vertical axis becomes dominant. Magnitude should return to around 1 g after each move. Record the actual orientation and readings.
4. If feasible, power off, disconnect the MPU module and boot the same diagnostic. Expect `MPU_INIT_ERROR` at the identity read, a `DISARMED` line and no sample output. Power off, reconnect the module and boot again to confirm successful initialization. Restore `Config::TEST_MODE` to `TestMode::None` after testing.

## Task tracker
States: NOT_STARTED, IN_PROGRESS, IMPLEMENTED_PENDING_VERIFICATION, VERIFIED, MERGED, BLOCKED. Do not mark MERGED until actual git history/owner confirms. Hardware evidence must be real. A user-confirmed waiver is separate from a pass.

| ID | State | Build | Hardware | Notes |
|---|---|---|---|---|
| T01 | MERGED | normal and uptime diagnostic passed | owner board check passed | Merge `78d6897` |
| T02 | MERGED | updated normal and both diagnostics passed | owner retest passed | Tip 171f429 present on master. Optional missing-module check unreported. |
| T03 | MERGED | raw 40 MotionLatch and raw 50 normal rebuilds passed | V06 and awake V07 passed at raw 50 | Tip `592b99f` on `master` with normal mode selected. Raw 40 sensitivity trial pending. |
| T04 | MERGED | normal and ButtonLifecycle builds passed, eight fake-time assertions passed | Owner confirmed short press, held press, disarm and both cancellation paths. Countdown and reset-to-DISARMED observed. | Merge `6c32bdb`. |
| T05 | IMPLEMENTED_PENDING_VERIFICATION | both Arduino builds and nine lifecycle assertions passed; native tilt suite compiled, execution blocked | Owner completed the hardware merge-gate checks, including slow tilt with impact disabled and transient rejection | Windows Application Control blocked the native test executable. |
| T06 | IMPLEMENTED_PENDING_VERIFICATION | Three Arduino builds and fake-bus tests passed | Owner verified V08 cadence, identity and four-sample reports. Tilt cleared during a burst. Impact reports observed between bursts. | Impact recognition inside a burst still to capture. |
| T07 | NOT_STARTED | pending | pending | |
| T08 | NOT_STARTED | pending | pending | |
| T09 | NOT_STARTED | pending | pending | |
| T10 | NOT_STARTED | pending | pending | |

## Approved decisions / open risks
- HW-03 permits the installed MPU6050 breakout to use its onboard 4.7 kΩ AD0 pull-down with the header unconnected. The configured I²C address is `0x68`. Replacement breakouts without a confirmed pull-down use an AD0-to-GND jumper.
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

For a focused suppression check, first complete the latch probe. Confirm two deliberate taps about 500 ms apart each produce a report at the normal 100 ms setting. Temporarily set `IMPACT_SUPPRESSION_MS` to 1000, rebuild MotionLatch and repeat the pair. Start at least one second after the last reported impact. The first verified notification should produce `IMPACT_DETECTED` and the second `IMPACT_SUPPRESSED` with the same reported count and a higher suppressed count. Restore 100 ms after the check.

## Verification evidence index
Add entries keyed V01..V14 with date, firmware commit/config, procedure, expected, observed evidence, pass/fail/pending/waived. Do not store fabricated serial logs as actual observations. V14 remains deferred.

- **V01 / V06–V07 AD0 wiring update, reported 2026-11-26.** The owner confirmed the breakout has a 4.7 kΩ pull-down and reported correct MPU6050 operation after disconnecting the AD0 jumper. The firmware build, startup address/identity and register capture, and latch/acknowledgement observations for this wiring were unreported. This change updates documentation only. No new build or board test was run by Codex.

- **V01 build and T01 board observations, 2026-11-25.** Uncommitted T01 on base `3f15c02`. Normal mode and `FoundationUptime` both compiled for ESP32 Dev Module on Arduino-ESP32 3.3.12. The owner observed serial-reported I2C initialization, 80 MHz actual CPU frequency, a timestamped `DISARMED` line and advancing diagnostic uptime. After deliberate reset, the owner observed uptime restart near zero and advance as expected. The T01 board check passed. Sensor identity and missing-sensor arming checks are deferred to their respective implementation tasks.
- **V01 T02 build and MPU board check, 2026-11-25.** Uncommitted T02 on base `78d6897`. All three variants compiled for ESP32 Dev Module on Arduino-ESP32 3.3.12. Identity, register readbacks, approximate 1 g stationary magnitude and rotation/sign are pending board evidence. Missing-MPU startup handling is also pending a board check if feasible. The arming gate is covered by later lifecycle work.
- **V01 T02 first MPU startup, 2026-11-25.** Owner tested the original normal build before the wake-order change. Identity read passed with `0x68`. Configuration readback failed at `0x1A` with actual `0x00` and expected `0x01`. Firmware reported the fault and stayed `DISARMED`.
- **V01 T02 MPU retest, 2026-11-25.** Owner reported passing board tests on the revised firmware with `MpuAcceleration` selected and appropriate magnitude readings. Startup success follows from the diagnostic running. Optional missing-module check unreported.
- **V06 T03 first latch probe, 2026-11-26.** Owner flashed the MotionLatch build on Arduino-ESP32 3.3.12. Identity and all 11 startup register readbacks matched, including INT_PIN_CFG 0x20. Motion-only enable read back 0x40. GPIO4 sampled HIGH before the first acceleration read and LOW immediately afterward. At 250 ms the status transfer succeeded with motion bit clear and GPIO4 LOW. At uptime 00:00:00.660 the diagnostic reported MOTION_FAULT, successfully disabled/cleared the interrupt and stayed DISARMED. The trace establishes neither the source of the initial HIGH nor why it fell. Deliberate tap timing and INT wire continuity were unreported. With power off verify the MPU INT header connects to ESP32 GPIO4 (previous wiring used GPIO17). Reset with the module stationary, note whether the probe starts by itself and, if it waits, tap once. If the failure persists with verified wiring, compare MPU INT and GPIO4 during the probe, preferably alongside I2C. V07 tap counts remain pending.
- **V06 T03 rewired board follow-up, 2026-11-26.** The owner rewired the board and reported matching MPU startup readbacks with no latch messages when tapping. The excerpt ends at DISARMED and omits the mode banner and the motion-enable confirmation. The workspace was set to TestMode::None when checked. The trace is consistent with normal mode. Repeat with the MotionLatch banner and 0x40 enable confirmation before drawing a conclusion about the rewired interrupt path.
- **V06/V07 T03 passing MotionLatch retest, 2026-11-26.** The owner confirmed the preceding no-response build used the wrong mode. After rewiring, the raw 50 diagnostic reported identity 0x68, matching startup readbacks including MOT_THR 0x32, and motion-only enable 0x40. `LATCH_HOLD` showed GPIO4 HIGH before and after a successful acceleration read. `LATCH_ACK` showed GPIO4 HIGH throughout the sampled checks over 250 ms, the motion bit set, and GPIO4 LOW after successful acknowledgement. The log contains 35 consecutive `IMPACT_DETECTED` reports from uptime 00:00:04.197 to 00:05:08.700 with zero suppressed notifications reported. The owner observed that strong taps were needed. V06 and the awake portion of V07 pass at raw 50. Sleep wake remains for T08. The diagnostic-only raw 40 sensitivity trial still awaits board data on lighter taps and stationary behavior.
- **T03 sensitivity trial build.** Arduino CLI passed both builds for `esp32:esp32:esp32` with the same CLI and config as above. The MotionLatch build used the workspace sketch and `--build-path build-t03-MotionLatch/firmware`. The normal build used `build-t03-threshold-normal/ESP32_Sleep_PoC`, a copy with `TestMode::None` selected, and `--build-path build-t03-None/firmware`. Flash usage was 395852 and 393704 bytes respectively. The workspace still selects MotionLatch for the owner retest. Restore None before merge.
- **T03 rapid-tap follow-up, 2026-11-26.** The owner reported no impact detections during rapid repeated tapping and could not reproduce suppression. The flashed threshold and a trace for this attempt were unspecified. The suppression counter requires a second acknowledged motion notification within the window after the last reported event. Multiple taps can share one latch, and a tap that produces no motion status gives the suppression logic nothing to count. Suppression remains unverified on the board. The passing raw 50 latch and awake tap observations above satisfy the T03 hardware merge gate.
- **T03 suppression retest, 2026-11-26.** The owner observed `IMPACT_SUPPRESSED` on the board with a temporary 1000 ms window. Exact event timestamps and the threshold used were unreported. The workspace is back to the 100 ms setting and `TestMode::None`.

### T04 BOOT lifecycle · 2026-11-26 · `feat/T04-button-lifecycle`

- Started with a clean tree at `592b99f`, the T03 tip on `master`. T03 latch and awake motion verification is recorded above.
- `Button` accepts one command after 20 ms of debounce followed by 300 ms of continuously observed LOW. A release observed during qualification restarts the hold. Stable release permits a new command. A button held at runtime initialization must be released before it can issue a command. `wakeMasked()` and `nextDeadline()` expose the BOOT state and poll or qualification deadline for later sleep integration.
- `DeviceController` starts DISARMED, sets the reference timing deadline at press acceptance +8 s, and requests diagnostic arming after a full 2 s timing window. A late start of that window extends the countdown. A qualified press cancels arming or disarms, and takes priority if it coincides with a countdown deadline. The optional LED is solid during ARMING and off in DISARMED, ARMED and FAULT.
- `TestMode::ButtonLifecycle` reports `ZEROING_SKIPPED` when the timing hook completes. Diagnostic arming enables MPU motion, clears stale status and resets motion counters through the T03 driver. Disarming masks and clears motion. Required MPU startup failures refuse arming when impact is enabled. Unrecoverable motion errors stop the diagnostic in FAULT and require a deliberate reset. Normal mode remains selected in the workspace. Real reference validation and full monitoring are pending.
- Both Arduino CLI builds passed with `esp32:esp32:esp32` and Arduino-ESP32 3.3.12. Normal used 393696 bytes of flash and 37380 bytes of global RAM. ButtonLifecycle used 396796 bytes of flash and 37460 bytes of global RAM. The diagnostic compile produced `t04_lifecycle.cpp.o` with all eight compile-time fake-time assertions evaluated. They cover bounce, the 319/320 ms boundary, held and repeated presses, release after startup, countdown, cancellation, late deadlines, faults and reset, with timestamps above 32-bit range.

Run from the repository root to reproduce the two builds and fake-time checks. The diagnostic build uses an ignored copy of the current sketch.

```powershell
$cli = 'D:/ArduinoIDE/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe'
$configPath = 'C:/Users/przemek/.arduinoIDE/arduino-cli.yaml'
& $cli compile --config-file $configPath --fqbn esp32:esp32:esp32 --build-path build-t04-normal ESP32_Sleep_PoC

$stage = 'build-t04-diagnostic/ESP32_Sleep_PoC'
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Copy-Item -Path ESP32_Sleep_PoC/* -Destination $stage
Copy-Item -LiteralPath tests/t04_lifecycle.cpp -Destination $stage
$stagedConfig = Join-Path $stage config.h
$content = Get-Content -LiteralPath $stagedConfig -Raw
$content.Replace('TEST_MODE = TestMode::None;', 'TEST_MODE = TestMode::ButtonLifecycle;') |
    Set-Content -LiteralPath $stagedConfig -Encoding ascii -NoNewline
& $cli compile --config-file $configPath --fqbn esp32:esp32:esp32 --build-path build-t04-diagnostic/firmware $stage
```

Owner board checks for V02/V03 and diagnostic motion.

1. Verify wiring as above. With BOOT released, upload the ButtonLifecycle build using the staged sketch or select `TestMode::ButtonLifecycle` in Arduino IDE. Open Serial Monitor at 115200 baud and reset. Capture `Mode BOOT lifecycle test` and `DISARMED` with the optional LED off.
2. Tap BOOT briefly and confirm no lifecycle event. Hold BOOT through the whole countdown and confirm exactly one `ARMING_STARTED`, then `ZEROING_STARTED` around +8 s and `ZEROING_SKIPPED` and diagnostic `ARMED` around +10 s. The optional LED is solid during the countdown and off when armed. Keep holding briefly after `ARMED` and confirm the hold produces no second command.
3. Release BOOT, then hold again to disarm once. From DISARMED, hold to start arming, then release. A release leaves arming underway. For the first cancellation trial, give BOOT a second hold of about 1 s around +2 s after `ARMING_STARTED`. For the second trial, start a fresh countdown, release, then give BOOT a second hold as soon as `ZEROING_STARTED` appears. In each trial, expect `DISARMED | arming cancelled` and no later arm from that attempt. Complete a fresh countdown, then reset while diagnostic ARMED with BOOT released and confirm a fresh DISARMED startup.
4. With impact enabled, try a deliberate known-detectable tap while disarmed, during countdown, and after diagnostic arming. Expect impact reports only while diagnostic ARMED. With the board still, disarm, rearm, and check that no stale impact is reported. The first detected tap in the new session should show `count=1`. If feasible, power off, disconnect the MPU, and boot the diagnostic again. Expect `MPU_INIT_ERROR` and `ARMING_REFUSED | FAULT` on a qualified press. Restore the sensor with power off.

The owner reported that a short press did nothing, a long hold through arming issued one command, and a subsequent long hold disarmed. The supplied log shows power-on followed by DISARMED at 00:00:00.405, ARMING_STARTED at 00:00:06.314, ZEROING_STARTED at 00:00:14.314, ZEROING_SKIPPED at 00:00:16.314, and diagnostic ARMED at 00:00:16.316. The MPU identity and startup readbacks matched. The log shows IMPACT_DETECTED count=1 at 00:00:26.551 and DISARMED at 00:00:28.861.

In the first cancellation attempts, releasing the initial press let the countdown continue to ARMED, as specified by ARM-03. The owner then confirmed cancellation with a second qualified hold in both windows. The first trial logged ARMING_STARTED at 00:00:01.985 and DISARMED with arming cancelled at 00:00:03.388, 1.403 s into the countdown. The second logged ARMING_STARTED at 00:00:14.376, ZEROING_STARTED at 00:00:22.376 and DISARMED with arming cancelled at 00:00:23.161, 0.785 s into the reference timing window. The supplied trace shows no ARMED event for either trial.

The T04 merge gate in TASKS.md was met. The owner selected ButtonLifecycle for the board test. The full motion isolation and stale-event rearm board checks remain unreported. At T04 the diagnostic stayed awake and the reference timing hook supplied no gravity reference.

### T05 reference and tilt · 2026-11-26 · `feat/T05-tilt`

- Started with a clean tree at `d838ecf`. The verified T04 merge `6c32bdb` is present on the branch. The tracked build selects `TestMode::None`. Select `TestMode::ButtonLifecycle` to exercise T05.
- During the final 2 s of countdown, collect one acceleration sample per 10 ms slot. Accept at least 180 of 200 samples, zero invalid reads, mean vector magnitude of 0.85–1.15 g, and RMS vector residual at or below 0.03 g. A rejected window produces `ZEROING_RETRY` and starts a fresh 2 s window. Keep the board flat and stationary during zeroing. The collector accepts any stationary gravity direction because the mounting axes are unspecified.
- Arm with a fixed unit gravity vector and its measured magnitude. The first tilt check is due at arm +1 s. Use the median of three angles seeded at 0° from the reference. An invalid or missed check clears persistence and makes the angle unavailable until three consecutive valid samples rebuild the median. The confirmed state survives transient interruptions. The next sample stays on the original schedule, with skipped slots counted. Persistence starts at the time of the first qualifying filtered observation. Scheduling jitter can move a transition to the next check if five seconds have yet to elapse.
- The BOOT diagnostic retains press qualification, cancellation and disarm. Zeroing acquires the MPU even with impact disabled. Successful arming enables motion if configured. Three consecutive MPU acceleration read failures across zeroing or tilt enter FAULT, stop monitoring and disable and clear any armed motion interrupt. Restore the sensor and reset before retrying. Tilt checks use ordinary acceleration reads. Motion acknowledgement remains in the MPU driver.
- Both Arduino CLI builds passed using the installed Arduino-ESP32 core with approved access after the sandbox denied access to the Arduino package directory. Normal used 393648 bytes of flash and 37380 bytes of global RAM. ButtonLifecycle used 403216 bytes of flash and 37628 bytes of global RAM. The diagnostic build compiled the nine fake-time lifecycle assertions, including retry and cancellation.
- The native tilt and reference suite compiled with Zig 0.15.2 and `-Wall -Wextra -Werror`. Windows Application Control blocked the generated executable both inside and outside the sandbox. Runtime assertions remain pending on a host that permits running the binary. `git diff --check` passed.

Run from the repository root. Use the CLI and config paths in the T04 handoff.

```powershell
& $cli compile --config-file $configPath --fqbn esp32:esp32:esp32 --build-path build-t05-normal ESP32_Sleep_PoC

$stage = 'build-t05-diagnostic/ESP32_Sleep_PoC'
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Copy-Item -Path ESP32_Sleep_PoC/* -Destination $stage
Copy-Item -LiteralPath tests/t04_lifecycle.cpp -Destination $stage
$stagedConfig = Join-Path $stage config.h
$content = Get-Content -LiteralPath $stagedConfig -Raw
$content.Replace('TEST_MODE = TestMode::None;', 'TEST_MODE = TestMode::ButtonLifecycle;') |
    Set-Content -LiteralPath $stagedConfig -Encoding ascii -NoNewline
& $cli compile --config-file $configPath --fqbn esp32:esp32:esp32 --build-path build-t05-diagnostic/firmware $stage
```

Run the fake-time tilt and reference tests with a native C++17 compiler. For Zig 0.15.2, set `$zig` to the path of `zig.exe` and run from the repository root.

```powershell
New-Item -ItemType Directory -Force -Path build-t05-host | Out-Null
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $PWD 'build-t05-host/cache'
& $zig c++ -std=c++17 -Wall -Wextra -Werror -I ESP32_Sleep_PoC ESP32_Sleep_PoC/TiltDetector.cpp tests/t05_tilt.cpp tests/t04_lifecycle.cpp -o build-t05-host/t05_tilt.exe
if ($LASTEXITCODE -eq 0) { & ./build-t05-host/t05_tilt.exe }
```

Owner board evidence for V03–V05.

- The T05 trace shows a power-on reset into `DISARMED` at 00:00:00.417. The first zeroing window began at 00:00:09.800, exactly 8 s after `ARMING_STARTED`. Three consecutive 2 s windows were rejected for movement with 200, 199 and 199 samples. On the fourth window, the stationary board reported `ZEROING_OK` at 00:00:17.800 with 199 samples, 0.969 g mean vector magnitude and 0.0053 g RMS residual. Diagnostic `ARMED` followed at 00:00:17.802.
- During the steep-tilt trial, the T05 trace shows one `TILT_DETECTED` at 00:00:34.802 (51.42°) and one `TILT_CLEARED` at 00:00:42.802 (0.19°) after release. The owner then disarmed at 00:00:56.166. The trace records recognition times. The physical movement and release times are unknown, so step-to-event latency remains unmeasured.
- A separate trace with the older T04 startup banner also contains real zeroing and tilt events. It shows stationary zeroing with 200 samples, 0.970 g and 0.0050 g RMS residual, two tilt detect/clear cycles, and impact reports and suppression while tilt monitoring was active.
- The follow-up T05 trace confirms `Impact off` and `ARMED | lifecycle diagnostic | motion off | tilt on`. Stationary zeroing collected 200 samples (0.970 g, 0.0054 g RMS residual). The owner's slow-tilt trial produced one `TILT_DETECTED` at 00:00:32.345 (45.02°) and one `TILT_CLEARED` at 00:00:40.345 (0.20°). The owner reported no transition during or after the short transient trial. These results complete the hardware checks in the T05 merge gate. The working tree is back to `TestMode::None` with impact enabled.

Board checklist for reproduction and remaining trials.

1. Follow the wiring and Serial Monitor steps above. Upload the staged ButtonLifecycle diagnostic with BOOT released. Confirm DISARMED at boot. With the board flat and still, qualify a BOOT hold and release. Expect `ARMING_STARTED`, `ZEROING_STARTED` around +8 s and `ZEROING_OK` followed by diagnostic `ARMED` around +10 s. Capture sample count, magnitude and residual. Keep it flat through several tilt checks.
2. Disarm and start another countdown. After `ZEROING_STARTED`, rock the board repeatedly throughout the window. Expect `ZEROING_RETRY` and another `ZEROING_STARTED`. Hold it still through the fresh 2 s window and expect `ZEROING_OK` and `ARMED`. In a separate trial, cancel during a retry window with a qualified BOOT hold and verify DISARMED. Reset with BOOT released and verify DISARMED.
3. From a stationary reference, briefly tilt to about 40° and return within 3 s. Expect no tilt transition. Then quickly move to about 40° and hold. Expect one `TILT_DETECTED` about 6–7 s after the physical step under typical cadence. Keep holding to check for repeats. Move to about 29° and hold for at least 8 s, then return to flat. Expect one `TILT_CLEARED` after the return. Record fixture angles and elapsed times. Use the native tests to verify the exact inclusive 30° and 27° boundaries.
4. Slowly raise the board past 30° and hold. Repeat with the opposite incline, sideways and combined roll/pitch, returning flat long enough to clear each time. Rotate the board about the vertical axis while flat and verify no tilt event. To isolate tilt from GPIO4, repeat the slow-tilt trial in a diagnostic build with `ENABLE_IMPACT_DETECTION = false`. Restore the feature switch after the test.
5. With motion enabled, verify a known-detectable tap still prints `IMPACT_DETECTED` during tilt monitoring. Disarm and rearm at a different stable orientation, then hold that orientation and verify the new session starts normal. Restore `TestMode::None` after board testing.

The remaining T05 merge gate item is execution of the native suite. The board traces leave exact physical step timing, other orientations and yaw, cancellation during a retry, and rearming at a different orientation unverified. Sustained linear acceleration can still look like inclination. The diagnostic remains awake, with periodic environmental reports and explicit sleep deferred.

### T06 BME280 acquisition · 2026-11-26 · `feat/T06-bme`

- Started from a clean tree at `3ced900`. The T05 tip is present on `master` and its board checks are recorded above.
- `EnvironmentSensor` probes `0x76` then `0x77`, accepts only chip ID `0x60`, resets and configures the chip asleep, and checks the register settings. It writes humidity before `ctrl_meas` so the setting takes effect. The driver uses checked Wire transfers and a single eight-byte read for each conversion. The installed Adafruit BME280 2.3.0 starts conversions in normal mode during initialization and its public reads do not propagate transport errors. The driver instead implements the compensation in Appendix A.1 of the [Bosch BME280 datasheet, BST-BME280-DS001-24 rev. 1.24](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bme280-ds002.pdf).
- The datasheet gives 46 ms typical and 53 ms maximum for T ×2, RH ×4 and P ×16. After each successful trigger write the driver waits 60 ms before the first status check, rechecks busy status at 5 ms intervals, and times out at 150 ms. Four independent forced conversions take about 240 ms plus processing in the awake diagnostic when each completes by the first check. These figures are calculated, and sensor timing remains unmeasured on the board.
- The diagnostic schedules its first burst at arm +10 s and keeps the original start-to-start schedule. It logs skipped slots when the scheduler falls at least a full period behind or a prior burst is still pending. BOOT, motion and tilt continue to run between conversion checks. A burst error discards all partial samples and retries on the next slot. Three consecutive failures enter FAULT and disable and clear motion. A required BME280 missing or failing initialization blocks arming. Disarm or another monitoring fault clears any pending burst. Recovery from fault requires reset after correcting the problem, then a new BOOT arm. The tracked configuration selects `TestMode::None`.
- Arduino CLI confirmed core 3.3.12. Normal, Environment and ButtonLifecycle all built for `esp32:esp32:esp32` after approval for access to the installed core. Flash/global RAM in bytes were 393644/37380, 408300/37748 and 403200/37628 respectively. Only the Environment binary contained the environmental start and report markers. `git diff --check` passed.
- The final fake-bus suite and the existing T05 tilt regression passed under WASI, with the T04 lifecycle assertions evaluated at compile time. The T06 suite covers both addresses, identity and calibration rejection, four fresh samples with expected means and units, a guard measured after the trigger write, early ticks, busy status, timeout, rejected writes, short reads, invalid samples and cancellation. Native Windows execution was intermittently blocked by Application Control for T06 and remained blocked for T05.

Build from the repository root. Use the CLI and config paths in the T04 handoff. The diagnostic copies differ from the tracked sketch only in the mode selection.

```powershell
foreach ($mode in 'normal', 'Environment', 'ButtonLifecycle') {
  $sketch = 'ESP32_Sleep_PoC'
  if ($mode -ne 'normal') {
    $sketch = "build-t06-$mode/ESP32_Sleep_PoC"
    New-Item -ItemType Directory -Force $sketch | Out-Null
    Get-ChildItem ESP32_Sleep_PoC -File | Copy-Item -Destination $sketch
    $conf = Join-Path (Resolve-Path $sketch) 'config.h'
    $source = [IO.File]::ReadAllText($conf)
    [IO.File]::WriteAllText($conf, $source.Replace('TEST_MODE = TestMode::None', "TEST_MODE = TestMode::$mode"))
  }
  & $cli compile --config-file $configPath --fqbn esp32:esp32:esp32 --build-path "build-t06-$mode/firmware" $sketch
  if ($LASTEXITCODE -ne 0) { break }
}
```

Run the fake-bus and existing tilt/lifecycle checks with Zig 0.15.2 and Node 22.22.0. Set `$zig` and `$node` to the respective executable paths. The WASI target runs the tests without generating a native Windows executable.

```powershell
New-Item -ItemType Directory -Force build-t06-host | Out-Null
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $PWD 'build-t06-host/cache'
& $zig c++ -target wasm32-wasi -std=c++17 -Wall -Wextra -Werror -I tests/fake_bme_wire -I ESP32_Sleep_PoC ESP32_Sleep_PoC/EnvironmentSensor.cpp tests/t06_environment.cpp -o build-t06-host/t06_environment.wasm
& $zig c++ -target wasm32-wasi -std=c++17 -Wall -Wextra -Werror -I ESP32_Sleep_PoC ESP32_Sleep_PoC/TiltDetector.cpp tests/t05_tilt.cpp tests/t04_lifecycle.cpp -o build-t06-host/t05_tilt.wasm
$runner = "const fs = require('node:fs'); const { WASI } = require('node:wasi'); for (const file of process.argv.slice(1)) { const wasi = new WASI({version:'preview1'}); const mod = new WebAssembly.Module(fs.readFileSync(file)); const code = wasi.start(new WebAssembly.Instance(mod, wasi.getImportObject())); if (code) process.exit(code); console.log(file, 'passed'); }"
& $node --no-warnings -e $runner build-t06-host/t06_environment.wasm build-t06-host/t05_tilt.wasm
```

Owner board results from the T06 Environment diagnostic on Arduino-ESP32 3.3.12.

- Startup showed BME280 at `0x76` with chip ID `0x60`, four forced samples, T ×2, RH ×4, P ×16 and IIR off. The board booted DISARMED and armed at 00:00:15.205 after stationary zeroing.
- The first burst started exactly 10.000 s after arming. All ten burst starts in the trace were 10.000 s apart. Each produced one four-sample report after 247–248 ms. Report intervals ranged from 9.999 to 10.001 s. The reported values ranged from 21.93 to 26.56 °C, 43.83 to 75.53 %RH and 964.23 to 964.36 hPa.
- The trace shows two impact detections and two suppressed notifications between bursts. Tilt triggered at 00:01:37.205 (59.02°) and cleared at 00:01:45.206 (0.28°), 1 ms after a burst started. That burst reported normally. An impact between `ENV_BURST_STARTED` and `ENV_REPORT` remains the outstanding merge-gate observation. Disarm during acquisition, rearm and missing-sensor refusal are also unverified on the board. The owner's working configuration selects `TestMode::Environment` for testing.

Board checklist for reproducing the T06 observations and completing the remaining checks.

1. With power off, confirm BME280 power at 3.3 V, common ground, SDA GPIO21 and SCL GPIO22. Select `TestMode::Environment` and upload to ESP32 Dev Module. Open Serial Monitor at 115200 baud. Capture `Mode BME280 acquisition test`, `DISARMED`, the detected address (`0x76` or `0x77`), chip ID `0x60` and the ready line showing ×2/×4/×16 and IIR off. Keep BOOT released during reset.
2. Arm with a qualified BOOT hold and keep the unit flat through zeroing. Capture `ARMED` and at least five pairs of `ENV_BURST_STARTED` and `ENV_REPORT`. Check the first start is around arm +10 s, starts remain about 10 s apart, and each report gives four samples and plausible local °C, %RH and hPa. Record report-to-report deviations against the V08 suggested ±250 ms tolerance and the start-to-report time. If a bus analyzer is available, use it to verify four writes of `0x55` to `0xF4`, each followed by one eight-byte sample read from `0xF7`.
3. Tap around expected burst starts and capture an `IMPACT_DETECTED` between a start and its report. Sustain an inclination above 30° across a burst, then return flat. Capture tilt detect/clear while the environmental cadence continues. In a separate trial, time a qualified BOOT hold to disarm between a burst start and its report. Check for no post-disarm report and verify the first burst after rearm begins at the new arm time +10 s.
4. If feasible, power off, disconnect only the BME280 and boot the same diagnostic. Expect `ENV_INIT_ERROR` and `ARMING_REFUSED` on a qualified hold. With power off reconnect the sensor, reset, and explicitly arm again. Restore `TestMode::None` after testing.
