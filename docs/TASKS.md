# Implementation tasks — Rev 0.6

This is an ordered backlog, not an instruction to implement everything in one session. Each task is one reviewable feature branch and normally one Codex chat. Dependencies refer to previously merged work; the owner may split a task further if debugging becomes large. Read only the selected task and its dependency status. All tasks must preserve prior working behavior and compile on the recorded target. No automatic merge. For each task, update `docs/STATUS.md` with actual results; use pending for physical tests until owner confirms. Tests described here are expected behaviors, not evidence. Configuration values and requirement IDs refer to REQUIREMENTS.md.

## Sequence at a glance
| ID | Branch | Functionality | Depends on |
|---|---|---|---|
| T01 | feat/T01-foundation | Reproducible Arduino skeleton and configuration | none |
| T02 | feat/T02-mpu-accel | MPU6050 acceleration driver | T01 |
| T03 | feat/T03-motion-interrupt | Autonomous latched impact detector | T02 |
| T04 | feat/T04-button-lifecycle | BOOT press qualification and lifecycle | T03 |
| T05 | feat/T05-tilt | Zeroing and tilt state machine | T04 |
| T06 | feat/T06-bme | BME280 non-blocking acquisition | T05 |
| T07 | feat/T07-scheduler | Deadline-driven integration, awake baseline | T06 |
| T08 | feat/T08-light-sleep | Timer + GPIO wake integration | T07 |
| T08A | feat/T08A-sleep-trace | Opt-in light-sleep trace test mode | T08 |
| T09 | feat/T09-reporting | Serial reports, diagnostics and fault hardening | T08A |
| T10 | test/T10-integration | Hardware regression and baseline release | T09 |

Use temporary test modes, one selected mode at a time via compile-time config. Keep tests in the repo, not throwaway sketches with diverging drivers. Before T07, standalone test mode may use short cooperative tick/poll timing with CPU awake; no claim of power optimization yet. T01 records how to select each mode. Intermediate builds may expose only supported functionality and must clearly say which stage they are in. Do not label them full ARMED monitoring until integration.

## T01 — Foundation and reproducible build
Branch: `feat/T01-foundation` | Dependencies: none | Requirements: HW-01..06, SYS-01, SYS-03, DATA-01

Scope:
- Inspect repo (may be documentation-only); create Arduino-compatible sketch directory and minimal `setup()/loop()`; no sensor-specific drivers yet.
- Establish config.h and common types (Vec3, validity/result, 64-bit time), feature switches/test-mode mechanism, serial startup banner, monotonic uptime wrapper and optional LED abstraction. Initialize shared Wire with agreed pins/speed; disable radios using APIs supported by actual core. Do not activate sleep yet. Do not make unsupported feature claims.
- Provide `.gitignore` for build outputs, local paths/IDE caches; short build/upload notes. Do not commit secrets or machine-specific COM paths. Record installed toolchain/core/FQBN, dependency install commands if applicable. If Arduino CLI is available, record exact compile command; use Arduino IDE as fallback. No PlatformIO requirement.

Deliverable: minimal bootable firmware and reproducible build instructions. No unnecessary module stubs or fake sensor data.

Merge gate: compile succeeds for actual board selection; owner flashes and confirms banner, uptime and safe DISARMED/test mode, no unexpected resets. If tooling unavailable, status pending, not pass. T01 can merge only with owner confirmation or explicitly recorded waiver.

## T02 — MPU6050 continuous accelerometer driver
Branch: `feat/T02-mpu-accel` | Depends: T01 | Requirements: HW-02/03, IMP-01, SYS-04

Scope:
- Register-level driver with bounded I2C read/write helpers, identity verification, reset/init only in initialization/test context, correct clock/power settings and readback. Continuous XYZ accel, ±8 g, nominal 1 kHz output, DLPF 184 Hz; all gyro axes standby, disable unused features. Confirm register fields from documentation. No interrupt detection yet.
- Read six acceleration bytes coherently; signed conversion, scale for configured range, explicit validity/error. Diagnostic test mode prints at modest rate, not per internal sample. Record actual identity and readback.

Merge gate: compile; owner observes stable magnitude approximately 1 g flat and appropriate changes when rotated, verifies sign and scale, tests missing sensor handling if feasible. No claim of full calibration. Unit test conversion/sign if practical. Record real evidence. Do not implement T03 yet.

Board verification for HW-03 records how AD0 is held low, the configured MPU6050 I²C address, the `WHO_AM_I` result, and startup register readbacks.

## T03 — Autonomous motion detection and latch validation (critical risk)
Branch: `feat/T03-motion-interrupt` | Depends: T02 | Requirements: IMP-01..04, V06/V07

Scope:
- Confirm actual MPU6050 motion detector register map; document source revision and raw threshold ambiguity. Configure motion HPF 5 Hz, threshold raw 50, duration 1, latched active-HIGH push-pull GPIO4, explicit INT_STATUS acknowledgement, interrupt enable only in test/armed operation. Use bounded handling and clear stale status before enable/operation.
- Centralize acknowledge ownership. Provide event result + count/suppression (100 ms adjustable). Verify ordinary accel reads do not clear latch. Poll asserted pin in awake diagnostic mode or ISR flag only; no I2C/Serial in ISR. Enable/disable/clear interfaces for lifecycle.
- Add diagnostic latch-hold test: deliberately defer ack for a bounded test window while observing GPIO4, perform unrelated accel reads, then acknowledge and report state. Disable by default outside test mode. Record actual behavior.

Merge gate: compile; on hardware deliberate taps produce observable motion interrupts, pin remains asserted until intended ack, unrelated reads do not clear, acknowledgement deasserts under stationary conditions. Tap sensitivity is exploratory, not numeric acceptance. If latch or motion detector fails, stop and diagnose before downstream sleep integration; don't fake events or switch to data-ready IRQ. Document any tuning of raw threshold and reason. T03 is an early go/no-go for architecture.

With the HW-03 wiring, confirm that a tap asserts GPIO4, an acceleration read leaves it asserted, and explicit `INT_STATUS` acknowledgement releases it.

## T04 — BOOT control and lifecycle (awake)
Branch: `feat/T04-button-lifecycle` | Depends: T03 | Requirements: ARM-01..05, V02/V03

Scope:
- Pure button state logic with injected monotonic time: 20 ms debounce, 300 ms qualification, one command per press, stable release to rearm. Short press ignored; avoid blocking. Expose next qualification/poll deadline and whether active-low wake should be masked while held.
- Lifecycle DISARMED/ARMING/ARMED/FAULT, 10 s countdown with final 2 s reference hook, cancel/re-arm/disarm semantics, optional LED nonblocking, immediate lifecycle serial messages. At this stage diagnostic lifecycle may use an explicit test-only reference hook; do not present placeholder zeroing as valid. T05 replaces hook with real validation. Enable/disable motion in appropriate diagnostic lifecycle state; stale events not counted.

Merge gate: compile; fake-time tests for bounce, boundary around 300 ms, held press, release/repress, countdown/cancel/reset; owner confirms BOOT behavior on board. Label incomplete zeroing/monitoring in startup banner until T05/T07. No sleep implementation yet.

## T05 — Reference acquisition and tilt state machine
Branch: `feat/T05-tilt` | Depends: T04 | Requirements: ARM-03/04, TILT-01..04, V03..05

Scope:
- Real reference collector final 2 s at 10 ms schedule; stability/count/magnitude validation, retry if unstable. Preserve fixed normalized reference and original magnitude. No invented calibration values or assumed sensor Z sign. Integrate into lifecycle; enable motion on successful arm.
- Pure TiltDetector receiving injected time and Vec3: magnitude validity gate, median, 30°/27°, five-second persistence, one-second scheduled checks, reset candidate after invalid/missed sample, no catch-up, state change only. Provide angle validity/staleness and transition metadata. Avoid angle ambiguity in units.
- Host tests for flat, ±pitch/roll, combined axes, yaw (identical gravity), slow drift, 29/30/27° boundaries with numeric tolerance, return-to-normal, short spikes, sustained step, invalid/missing check, filter startup and rearm. Use fake time, no real sleeps in logic tests.

Merge gate: compile/test; owner confirms stationary zeroing, movement causes retry, sustained tilt triggers once, return clears once, slow tilt works without requiring motion IRQ, short transient does not trigger tilt; expected 6–7 s typical physical step delay (5 s after filtered crossing). No strict 1 s detection latency. Any new defaults documented.

## T06 — Non-blocking BME280 acquisition
Branch: `feat/T06-bme` | Depends: T05 | Requirements: HW-04, ENV-01..04, V08

Scope:
- Auto-detect 0x76/0x77 and verify BME chip ID. Select and record dependency/version or datasheet-based compensation implementation; prefer supported library and explicit register trigger. Ensure forced-mode configuration without unwanted initial conversion. Use x2/x4/x16 oversampling, IIR off; correctly configure humidity before ctrl_meas. Do not call blocking takeForcedMeasurement() in acquisition path.
- Implement independent state machine start/tick/nextDeadline/takeResult/cancel; trigger each of four samples, wait via returned deadline, status check, read fresh coherent values, compensated averages and unit conversion. Guarded initial wait 60 ms, bounded timeout, no partial results/stale sample; handle early wakes and cancellation. Test acquisition while CPU awake using same tick interface intended for later sleep.
- Use fake transport/clock tests where practical: no early read, four distinct samples, arithmetic mean/units, timeout/invalid read, cancellation, early wake, no duplicate conversions. Document actual sensor timing.

Merge gate: compile; owner sees plausible T/RH/hPa and four-sample reports, both motion and tilt continue during conversion, correct address and chip ID. Test 10 s acquisition cadence in diagnostic mode; final deadline integration T07. No current claims.

## T07 — Cooperative scheduler and full awake integration
Branch: `feat/T07-scheduler` | Depends: T06 | Requirements: ARM-04, ENV-01, TILT-02, IMP-03, PWR-02, SYS-01/04, V10/V11

Scope:
- One controller owns lifecycle and absolute deadlines; arm resets counters and schedules tilt +1 s, environment +10 s; BME conversion deadlines separate. Motion/BOOT serviced whenever pending, then due tilt, environment, reporting. Compute earliest deadline; do not move phase on unrelated events. Skip missed slots with counts, never fabricate catch-up samples or persistence. No blocking delay in armed path; use awake diagnostic pacing only if bounded and does not prevent event servicing.
- Integrate all drivers; clear stale state on disarm/rearm; no post-disarm BME publication. Recheck input lines and due work before idle. Establish interfaces needed for PowerManager. Inject fake time/I/O to test concurrent due tasks, event preemption, deadline math, missed check, cancellation and uptime >32-bit.

Merge gate: compile + deterministic tests; owner confirms full awake functional baseline: 10 s reports, 1 s checks, 5 s tilt persistence, BOOT disarm, rearm, taps during BME. Do not enable light sleep yet. This is the reference behavior for T08 regression.

## T08 — Explicit light sleep and wake handling
Branch: `feat/T08-light-sleep` | Depends: T07 | Requirements: PWR-01/02, ARM-02, SYS-01/02, V09/V10/V11

Scope:
- Implement PowerManager using native ESP-IDF APIs inside Arduino: timer wake to earliest deadline, GPIO4 HIGH, GPIO0 LOW, `esp_sleep_enable_gpio_wakeup`, `esp_light_sleep_start`; check return codes and actual installed version. Restore/disable stale timer configuration when needed. GPIO wake independent polarity. Ensure radio disabled and initial supported 80 MHz active setting.
- Handle all pending work on return; do not rely on a single wake reason. Guard near deadlines and active pin races; bounded retry or fault instead of wake storms. While BOOT held/qualifying, mask only BOOT wake and use timer polling until release; motion wake remains enabled. Handle asserted MPU line while awake and fault if cannot clear. UART flush outside timing-critical work with bounded behavior; don't sleep mid I2C. Keep BME conversion sleep eligible. Keep feature switch ENABLE_SLEEP to compare identical awake baseline.
- Track sleep-call elapsed time, entry/exit errors and wake call categories; esp_timer uptime across sleep. Distinguish observed pin causes from wake cause, no duplicate wake counts. No new deep sleep/ULP.

Merge gate: compile; actual board test mandatory unless explicitly waived: timer wake advances uptime without reset, MPU motion wakes from actual light sleep, BOOT wakes/disarms, held BOOT does not cause rapid repeated wakes, sleep persists between deadlines, impacts during BME wait are serviced, sleep disable gives previous functionality. Capture serial evidence and counters; no claim of electrical current. If board can't sleep, preserve working T07 and diagnose on branch.

## T08A — Light-sleep trace test mode

Use branch `feat/T08A-sleep-trace` after T08. Requirements DATA-03 and PWR-02 apply, with verification V15.

- Add `TestMode::SleepTrace` to `config.h`, keep `TestMode::None` as the checked-in default, and identify the selected mode and sleep state in the banner. Reuse the normal lifecycle, scheduler, sensors and power path. Respect `ENABLE_SLEEP`.
- Preserve the scheduler's timer deadline reasons and collect due work at wake. Capture the ESP-IDF cause and enabled GPIO observations on successful sleep return before normal handling changes the inputs. Emit the DATA-03 lines with the existing uptime format. Budget UART drain time and recheck the current deadline and inputs before entry, keeping the existing wake configuration, fault handling and absolute schedule.
- Build normal mode and SleepTrace with sleep enabled and disabled on `esp32:esp32:esp32` using Arduino-ESP32 `3.3.12`. Use fake time and I/O to check wake timestamps and durations, simultaneous due reasons, both GPIO observations, unresolved GPIO wake, UART deferral, entry cancellation and sleep rejection. Verify one entry announcement per attempt and one wake line per successful sleep return. Re-run the scheduler and power suites.
- On the board, capture a BOOT wake from disarmed idle and timer wakes for tilt, a BME280 burst start and a conversion check. While armed, tap to capture a GPIO4 wake and motion event. Verify one disarm during a held BOOT press, complete serial lines, continuous uptime, and four-sample reports on the 10 s phase. Check normal mode and SleepTrace with sleep disabled for zero sleep trace lines. UART transmission may shorten or skip the shortest sleep windows, especially during zeroing.

Merge gate requires successful builds, deterministic regression tests and owner board evidence for V15 or an explicit documented waiver. Record actual results in STATUS.

## T09 — Serial diagnostics and fault hardening
Branch: `feat/T09-reporting` | Depends: T08A | Requirements: DATA-01/02/03, SYS-01..04, V13

Scope:
- Complete human-readable event messages, 10 s successful BME report with required fields, recognition uptime, state, counts and errors, sleep stats with accurate units and denominator. Restrict per-wake and pre-sleep lines to SleepTrace as specified in DATA-03. Include wake/missed deadline/late processing, suppressed impacts, BME samples and failures. Report stale/unavailable tilt explicitly, disabled features explicitly; no stale data presented as valid.
- Review error paths throughout drivers, sensor availability/arm refusal, bounded I2C errors and conversion timeout, sleep error, latch failure, no automatic rearm, rate-limited error logging. Add fault/recovery tests and validate safe fault/disarmed state. No unrelated refactor or new data storage/communication.
- Investigate the [intermittent BME280 invalid sample observed during T08](STATUS.md#intermittent-bme280-invalid-sample) under ENV-03/04 and SYS-04. Add failure-only diagnostics for the exact rejection reason, conversion index, time since trigger, available raw sample bytes and status/configuration readback, and the most recent BOOT wake observation with its timestamp. Keep capture bounded and avoid per-wake output.
- Use fake time to inject short BOOT activity before and at BME conversion deadlines. Verify no early reads, extra conversions, partial reports, or schedule drift. If board reproduction is needed, use a timed trigger and compare sleep enabled with the awake baseline. Use the captured evidence to guide any validation or wake-related fix and add a regression for the identified failure. Retain four fresh conversions per successful burst and the existing failure recovery policy.

Merge gate: compile and deterministic tests; owner confirms readable reports and immediate events, no silent failures, no UART truncation or sleep-induced flood; simulate failure using safe diagnostic injection rather than live rewiring if possible. Record actual results and remaining issues.

## T10 — Integrated validation and release baseline
Branch: `test/T10-integration` | Depends: T09 | Requirements: V01..V13 and V15 (V14 deferred)

Scope:
- Review full firmware against requirement IDs and task history. Run reproducible build and all available pure tests. Prepare a manual hardware checklist mapping V01..V13 and V15 to steps, expected results, actual evidence and pass/fail/pending. Include a short V15 SleepTrace run. Where practical, perform owner-led stationary 10 min and approximately 30 min integrated runs in `TestMode::None`. Inspect schedules, stale events, uptime, unexpected resets and sleep utilization.
- Manual tap tests around 500 ms apart, tap during environmental burst, slow/fast tilt and recovery, button while asleep, disarm/rearm and power cycle. Record observed detections/misses/false alarms; no mandatory numerical tap ratio. No exact current gate. Do not claim tests performed by Codex unless actually executed with evidence.
- Fix small integration issues in scope; large defects get separate `fix/...` tasks/branches and explicit tracking before release. Record final versions and known limitations, update STATUS with release readiness.

Merge gate: owner reviews actual checklist. Required functional tests pass or documented explicit waiver; no unresolved blocking defect. Tag baseline after merge (e.g. `poc-v0.1.0`) only if owner chooses. Future power experiments start separate branches from baseline.

## Deferred experiments (not authorized baseline tasks)
- E01: Instrument real current, breakout/regulator overhead, CPU frequency sweep, compare sleep-enabled/disabled. Requires power measurement tools.
- E02: Compare deep sleep with timer + GPIO wake, retained state and timestamp reconstruction; 1 s tilt period makes it worth measuring. Do not assume lower average current. Preserve baseline on main until verified.
- E03: Experiment with low-power cyclic accelerometer or alternative motion settings; characterize missed taps versus power. No change to baseline without owner decision.
- E04: ULP-assisted tilt monitoring (likely requires RTC-compatible I2C/wiring changes); out of scope until explicitly approved.
