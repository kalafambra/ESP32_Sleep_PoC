# ESP32 Sleep Mode PoC — requirements and architecture
Revision: 0.4 | Baseline specification | 2026-11-25

## 1. Purpose and boundaries
Demonstrate low-power environmental acquisition and asynchronous physical-event monitoring. The ESP32 should sleep whenever no work is pending, while the MPU6050 independently detects short acceleration transients such as a finger tap on the enclosure. Tilt checks must detect slow changes as well as sudden sustained inclination. The intended product may run for days; the initial PoC is exercised for minutes. Power is an optimization objective, not a pass/fail threshold. The earlier 5 mA at 5 V figure is a benchmark only; analysis focuses on ESP32, MPU6050, BME280, not USB-UART, regulator or breakout overhead. Precise current testing is deferred. No microphone, audio, radio, network, persistent data logging, wall clock, deep sleep or ULP in v1. No claim of perfect impact detection.

## 2. Hardware and software
| ID | Requirement |
|---|---|
| HW-01 | Original ESP32-WROOM-32D carrier board, Arduino framework, C++; owner uses Arduino IDE and VS Code/Codex. Record actual board selection/FQBN and Arduino-ESP32 core in STATUS; do not guess installed versions. Prefer Arduino CLI for reproducible compile, if installed. |
| HW-02 | BME280 and MPU6050 share 3.3 V and GND, SDA GPIO21, SCL GPIO22. Initially 400 kHz I2C; configurable, fall back to 100 kHz only as an explicit diagnostic/config change. |
| HW-03 | MPU6050 AD0 to GND, address 0x68; INT to GPIO4 (not GPIO17); XDA/XCL unused. Verify identity and interrupt behavior on actual module. |
| HW-04 | BME280 auto-detect 0x76/0x77; verify chip ID 0x60 (do not accept BMP280 as BME280). |
| HW-05 | BOOT GPIO0 active LOW; GPIO2 status LED active HIGH or -1 to disable. UART TX GPIO1/RX GPIO3, 115200 baud. Never drive BOOT as output. Holding BOOT during reset may enter bootloader; runtime button operation only. |
| HW-06 | INMP441 not used; disconnect its supply/module for meaningful power measurements. No changes to agreed pinout without owner approval. |

## 3. Configuration defaults (single source in config.h)
Use descriptive units in identifiers; validate ranges and relationships. Defaults are pre-compilation settings, not runtime commands. Raw register fields are not physical units unless confirmed.

| Setting | Default | Notes |
|---|---:|---|
| I2C_SDA_PIN / I2C_SCL_PIN | 21 / 22 | Fixed wiring |
| MPU_INT_PIN / MPU_ADDRESS | 4 / 0x68 | Fixed wiring |
| BOOT_PIN / STATUS_LED_PIN | 0 / 2 | LED may be -1 |
| I2C_CLOCK_HZ | 400000 | Configurable |
| SERIAL_BAUD | 115200 | Configurable |
| CPU_FREQ_MHZ | 80 | Initial experiment; verify support |
| ENV_INTERVAL_MS | 10000 | Start-to-start schedule |
| ENV_SAMPLE_COUNT | 4 | Valid independent forced conversions |
| ENV_TEMP_OVERSAMPLING | 2 | Physical multiplier, not encoded bits |
| ENV_HUM_OVERSAMPLING | 4 | Physical multiplier |
| ENV_PRES_OVERSAMPLING | 16 | Physical multiplier |
| TILT_INTERVAL_MS | 1000 | Updated from 200 ms |
| TILT_THRESHOLD_DEG | 30 | Total inclination; proposed supported range 5–90° |
| TILT_HYSTERESIS_DEG | 3 | Must be smaller than threshold |
| TILT_PERSISTENCE_MS | 5000 | Updated from 300 ms; both transitions |
| TILT_FILTER_SAMPLES | 3 | Odd window; median of valid angle/cosine samples |
| TILT_MIN_VALID_RATIO / TILT_MAX_VALID_RATIO | 0.85 / 1.15 | Relative to stationary reference magnitude |
| IMPACT_THRESHOLD_RAW | 50 | Experimental register value, NOT calibrated g |
| IMPACT_DURATION_MS | 1 | Verify register semantics for actual device |
| IMPACT_SUPPRESSION_MS | 100 | Tunable, 0 disables |
| BUTTON_DEBOUNCE_MS | 20 | Proposed stable transitions |
| BUTTON_HOLD_MS | 300 | Continuous qualified press |
| ARMING_DELAY_MS | 10000 | Includes final zeroing window |
| ZEROING_TIME_MS | 2000 | Stationary reference window |
| ZEROING_SAMPLE_MS | 10 | ~100 Hz |
| ENABLE_SLEEP / ENABLE_STATUS_LED | true / true | No LED when armed; optional disable |
| ENABLE_IMPACT_DETECTION / ENABLE_TILT_DETECTION / ENABLE_ENV_ACQUISITION | true / true / true | Test isolation; report disabled features |

Proposed zeroing stability defaults (engineering choices, adjust after hardware tests): mean acceleration magnitude 0.85–1.15 g, RMS vector residual <=0.03 g, at least 90% of expected samples, no invalid read in accepted window. Check plausible flat orientation only if actual sensor mounting axes are known; no assumed Z sign. This is relative gravity reference zeroing, not full six-axis calibration.

## 4. Functional requirements
### ARM — lifecycle and control
- ARM-01: Power-up/reset always DISARMED. No automatic rearm. Initialize hardware and report detected devices/configuration. If required enabled sensor is unavailable, refuse arming and report fault.
- ARM-02: Accept BOOT after 20 ms debounce and >=300 ms continuously qualified LOW; short press ignored. One command per hold, release must be debounced before another command. Allow wake on LOW from light sleep. A stuck/held pin must not cause repeated immediate wake-ups: temporarily mask its wake source while held/being qualified and use timer checks (e.g. 20 ms) until safe to re-enable. Never disable motion wake just because button is held.
- ARM-03: After accepted press start 10 s countdown. Final 2 s collect zeroing samples while stationary. If unstable, remain ARMING, report failure and seek a fresh full stable 2 s window; no arm with invalid reference. Next valid press cancels arming or disarms ARMED state. These cancellation/retry semantics are explicit baseline design defaults.
- ARM-04: At successful arming reset per-session counters, filter/persistence, stale impact status, environmental schedule; enable monitoring and turn LED off (short arming confirmation optional before sleep). First tilt check at arm+1 s; first environmental burst at arm+10 s. Disarm cancels acquisition/schedules, disables/clears MPU motion interrupt and clears candidate state. BME forced conversion already in flight may finish but must not publish after disarm. Re-arm captures new reference.
- ARM-05: Status LED optional, non-blocking countdown indications, off in steady ARMED. Sensor fault must not be represented as healthy armed operation. Uptime is not reset by arming/disarming.

### TILT — independent gravity-vector inclination
- TILT-01: Store averaged reference vector and its magnitude from flat, level, stationary arming. Normalize reference and each valid current vector; compute angle acos(clamp(dot(unitRef,unitCurrent),-1,1)). Any direction of pitch/roll/combined inclination; pure yaw excluded. Reference never adapts while armed. Can compare cosines to avoid acos on routine checks.
- TILT-02: Every 1 s read latest acceleration, reject I2C errors, nonfinite/near-zero vector or magnitude outside configured relative bounds. Median of last 3 valid consecutive scheduled checks (seed from reference at arm). Magnitude gate and median mitigate transients but cannot perfectly separate gravity from sustained linear acceleration.
- TILT-03: NORMAL -> TILTED when filtered angle >=30° continuously at observed valid checks for >=5 s from first qualifying filtered check. TILTED -> NORMAL when <=27° for >=5 s. Inclusive thresholds; between 27 and 30 retain state and cancel candidate transition if its condition no longer holds. Emit one transition event, not repeated alarms. Invalid or missed checks break persistence and reset/reseed filter from subsequent valid data; do not count unknown time or synthesize catch-up samples. Retain last confirmed state while transiently invalid and mark angle stale/unavailable in report. Repeated read failure enters fault/disarms per SYS-04.
- TILT-04: Old 1 s end-to-end detection limit is retired. With 1 s checks, 3-sample median and 5 s persistence, sustained valid step typically reports in about 6–7 s (first filtered qualifying observation + 5 s); no hard 5 s physical latency guarantee. Verify no event before 5 s of qualifying observations, then event at first qualifying deadline thereafter. Both slow tilt and sustained step must work independently of motion interrupts.

### IMP — hardware motion/impact
- IMP-01: MPU6050 continuously active accelerometer XYZ, gyroscope XYZ standby, DMP/FIFO/temperature disabled, cyclic mode off. Initial ±8 g range, 1 kHz output, accelerometer DLPF ~184 Hz, motion HPF 5 Hz. Verify exact bit settings/register semantics against documentation and actual module; record readback. Use suitable internal clock with gyro disabled. No MPU reset during armed operation unless recovering to disarmed.
- IMP-02: Enable hardware motion interrupt (not data-ready); active HIGH, push-pull, latched, explicit INT_STATUS read clears; INT_RD_CLEAR disabled. Raw threshold 50 and duration 1 ms are experimental. Motion status ownership centralized in MPU driver; no other read clears latch. Initial latch/settling cleared just before arming. Keep accelerometer continuously powered while ESP32 sleeps.
- IMP-03: GPIO4 level HIGH wakes light sleep. Service pending status while CPU awake as well as after wake; inspect pending line before sleep, handle races by level wake. Do not perform I2C or Serial in ISR. Count/report verified motion status once per acknowledged notification subject to optional 100 ms suppression; acknowledge even suppressed notifications. Distinguish suppressed and reported counts for tuning. No blanket disabling interrupts during BME conversion or tilt checks.
- IMP-04: Report IMPACT_DETECTED immediately (target responsiveness about 1 s under normal operation, not a guaranteed timestamp of shock). Timestamp is CPU recognition uptime. No peak/waveform capture or dedicated tap classifier. Slow tilt should not generally trigger impact; rapid handling/ringing may. Latched status can merge close events. Manual deliberate taps approximately 500 ms apart, observe count and tune; no fixed count/pass ratio, no formal all-taps criterion. Manual testing may include 50 taps but is exploratory.

### ENV — environmental acquisition
- ENV-01: Every 10 s while armed, independent absolute start-to-start schedule, first burst at arm+10 s. No schedule reset on impacts/tilt events. Do not emit synthetic catch-up bursts if a deadline is missed; skip to future slot and record miss. Configurable period must accommodate acquisition.
- ENV-02: BME280 sleep between bursts. Four independent forced conversions; oversampling temperature x2, humidity x4, pressure x16, IIR off. Average compensated T °C, RH % and pressure hPa (Pa / 100). A record is local conditions at acquisition time, not a time-average over 10 s. Do not claim oversampling removes bias.
- ENV-03: Non-blocking state machine: trigger conversion, return to scheduler, sleep if possible, check completion/status after datasheet-derived time (~53 ms, initial guarded 60 ms), read fresh coherent sample, repeat until four. Bound conversion timeout (e.g. 150 ms per sample initially); never read stale sample or average partial/invalid burst as valid. Ensure humidity settings latched by ctrl_meas write; compensate using correct temperature/t_fine for same conversion. Prefer tested Bosch compensation or Adafruit BME280 public read methods with verified semantics and explicit register trigger; avoid blocking takeForcedMeasurement(), accidental initial forced conversion or modifying installed library. Record chosen dependency/version and validate actual API. No duplicate conversions on early interrupt wake.
- ENV-04: Emit exactly one human-readable package for each successfully completed burst. On failure log explicit ENV_ERROR, discard partial burst, retain independent schedule/retry later; no fabricated values. Keep impact and tilt service active during conversion wait.

### PWR/SYS — scheduling, faults, time and diagnostics
- PWR-01: Baseline explicit `esp_light_sleep_start()` and timer wake to earliest pending absolute deadline. GPIO4 HIGH and GPIO0 LOW independently configured with gpio_wakeup_enable + esp_sleep_enable_gpio_wakeup. Use verified core/IDF APIs, check return codes and avoid sleep with due work or asserted unhandled pin. Do not use delay() as substitute for hardware sleep. Disable radios; do not enable radio or modem services.
- PWR-02: One cooperative scheduler, no armed busy waits or repeated catch-up loops. Service motion, BOOT, due tilt, BME state, reports, then compute next deadline; recheck pending work before sleep. A timer wake is not necessarily a tilt check. Early wakes do not move schedules. Keep short bounded I2C operations, no concurrent bus owners. Flush queued UART TX before sleeping or verify documented buffering behavior; no loss of messages, no per-wake logging. Avoid sleeping in active bus transaction.
- SYS-01: Use 64-bit monotonic `esp_timer_get_time()` as uptime/deadline source across light sleep; store integer us or ms consistently. No wall clock, no 32-bit wrap failure. Timestamp outputs as HH:MM:SS.mmm with hours not wrapping at 24 h. Maintain sleep counters and elapsed time across sleep. Measure elapsed sleep-call time (includes entry/exit overhead); label approximate sleep utilization, not electrical current. Per-armed-session statistics reset at arm; uptime remains global.
- SYS-02: Diagnostics: timer/GPIO/other wake calls, motion/BOOT source observations (may overlap), sleep-call errors/skips, cumulative sleep-call and awake durations, armed utilization, impact/suppressed count, tilt transitions, BME sample/burst/errors, missed deadlines. Do not double count wake calls if two pins assert. Distinguish awake event handling from actual GPIO wake. Track peak lateness/processing duration if useful. No precise current acceptance gate.
- SYS-03: Enabled-feature switches for staged tests; report disabled features, never claim full functionality when isolated. No Wi-Fi/BLE, flash logging, deep sleep or ULP in baseline.
- SYS-04: All I2C calls and sleep APIs checked; finite timeouts/backoff. Transient read errors logged/rate-limited and invalid samples rejected; repeated failures (initially 3 consecutive failed operations per critical sensor, configurable) or unrecoverable wake/latch failure enter safe disarmed/fault state with reason, no wake storm. BME single burst failure retry next slot. Owner can reinitialize/rearm once recovered; no silent auto-rearm. Exact recovery policy documented if implementation changes it.

### DATA — serial contract
- DATA-01: Boot banner lists firmware/core if available, sensor identities, pinout, config and feature switches, state. Serial 115200. Immediate lines: `[HH:MM:SS.mmm] ARMING_STARTED`, `ZEROING_STARTED`, `ZEROING_OK` or `ZEROING_RETRY`, `ARMED`, `DISARMED`, `IMPACT_DETECTED count=N`, `TILT_DETECTED angle=X`, `TILT_CLEARED angle=X`, fault/error as applicable. Human readable; no exact timestamp of physical tap asserted. Do not require a rigid binary protocol.
- DATA-02: Periodic report includes uptime, armed duration, report number, temperature °C, RH %, pressure hPa, samples averaged, tilt angle or stale/unavailable, state/threshold, transitions, impact count/last recognized time, sleep mode, sleep-call duration, awake duration, approximate sleep utilization, wake stats, failures and missed deadlines. No unmeasured current estimate presented as measured value. Do not flood per periodic tilt check.

## 5. Architecture and interfaces
Selected: ESP32 explicit light sleep + autonomous continuous MPU6050 hardware motion interrupt + periodic host tilt checks + non-blocking BME280 conversions. At 1 s tilt period deep sleep may become competitive; measure later, not assumed inferior. Typical chip-level idle baseline around 1.3 mA at 3.3 V (0.8 ESP + 0.5 MPU, BME negligible), illustrative only. No validated average current or 5 V input prediction. Main CPU active frequency 80 MHz initially, evaluate later.

Suggested Arduino sketch layout (exact names may be refined in T01 and recorded): `ESP32_Sleep_PoC/ESP32_Sleep_PoC.ino`, `config.h`, `Types.h`, `MotionSensor.{h,cpp}`, `TiltDetector.{h,cpp}`, `EnvironmentSensor.{h,cpp}`, `Button.{h,cpp}`, `PowerManager.{h,cpp}`, `DeviceController.{h,cpp}`, `SerialReporter.{h,cpp}`. Tests/scripts outside sketch directory. Avoid placeholder modules that claim unsupported behavior. Keep public interfaces stable after merge; changes recorded in STATUS.

Suggested contracts: `MotionSensor` begin/configure, readAccel, enableMotion, readAndAcknowledgeMotion, interruptAsserted; `TiltDetector` pure reference/filter/transition logic receiving samples and injected timestamps; `EnvironmentSensor` start/tick/nextDeadline/takeResult/cancel; `Button` tick/nextDeadline/wake mask/one-shot press event; `PowerManager` sleepUntil and wake stats; `DeviceController` owns armed lifecycle, all deadlines and calls; `SerialReporter` emits typed events/reports. Define result structs with validity/error rather than zero on failure. Only MotionSensor acknowledges INT_STATUS; no unrelated driver reads do so. One owner of Wire/I2C context (cooperative, main task).

## 6. Verification matrix
| ID | Expected verification | Method |
|---|---|---|
| V01 | Reproducible build, correct IDs/addresses, missing sensor prevents full arm | Compile + board |
| V02 | Short BOOT ignored; long press one action; held/release/repress works | Fake-time tests + board |
| V03 | 10 s countdown includes final stable 2 s reference; unstable retry; reset disarmed | Fake-time + board |
| V04 | Reference near 0° (suggest within 2°); slow tilt any direction, yaw excluded | Pure logic + board |
| V05 | 30°/27° transitions after 5 s valid persistence, no chatter/invalid-time carryover | Pure logic + board |
| V06 | Latched MPU motion interrupt persists until explicit ack; unaffected by unrelated reads | Board; log/readback or meter if available |
| V07 | Manual taps wake CPU, recognizable events; ~500 ms spacing; record observations not numeric pass threshold | Board |
| V08 | Four fresh forced samples averaged; one report per 10 s slot (suggest +/-250 ms under quiet conditions, record deviations), units plausible | Fake state tests + board |
| V09 | Timer/MPU/BOOT wake work, no stuck-high/held-button sleep loop, no reboot, uptime advances | Board |
| V10 | Tap during BME conversion/serial output; tilt deadlines independent; no false synthetic records | Board + fake scheduler tests |
| V11 | Disarm while sleeping/while BME pending; rearm new reference; stale status not counted | Board |
| V12 | Stationary 10 min, integrated ~30 min operation, inspect false events, resets, timing, stats and sleep utilization | Board; exploratory false-event observation |
| V13 | Invalid reads/timeouts/missed deadlines handled, fault not healthy armed, recovery deliberate | Mock tests + safe board simulation |
| V14 | Measure current and compare sleep modes | Deferred, not a v1 acceptance gate |

## 7. References and open engineering risks
- ESP-IDF sleep: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/sleep_modes.html
- ESP timer: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/esp_timer.html
- Arduino ESP32: https://docs.espressif.com/projects/arduino-esp32/en/latest/
- Bosch BME280: https://www.bosch-sensortec.com/en/products/environmental-sensors/humidity-sensors-bme280 (datasheet linked there).
- Adafruit BME280 source: https://github.com/adafruit/Adafruit_BME280_Library (inspect actual installed version, not merely master).
- MPU6050: consult InvenSense MPU-6000/MPU-6050 product specification and register map, including documented revision differences in motion detection registers; record exact sources used in T03. Actual chip/clone behavior and detector filtering require physical verification.
- Risks: motion detector is not a tap classifier; latched events can coalesce; noisy wiring and regulator/breakout overhead may dominate; persistence interpretation is five seconds after first filtered qualifying check; no measured power yet. Timing/zeroing/impact settings marked proposed are tunable. Owner must approve requirement-level changes.
