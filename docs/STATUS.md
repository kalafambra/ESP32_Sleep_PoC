# Project status and handoff
Revision 0.4 — initialized before firmware implementation.

## Current state
- Documentation prepared; no firmware implemented, compiled, flashed or physically tested as part of this starter pack.
- Selected architecture: explicit ESP32 light sleep, continuous MPU6050 accelerometer with latched GPIO4 motion interrupt, 1 s tilt checks with 5 s persistence, BME280 four forced samples every 10 s.
- Next recommended task: T01, only when explicitly requested by owner.
- Hardware wiring change required relative to original: move MPU INT GPIO17 -> GPIO4. Owner approved; physical connection not confirmed.

## Environment (fill in T01)
| Item | Recorded value |
|---|---|
| Arduino IDE version | TBD |
| Arduino CLI version/availability | TBD |
| Arduino-ESP32 core version | TBD |
| Board selection/FQBN | TBD (ESP32-WROOM-32D module; carrier board exact model unknown) |
| OS / toolchain | TBD |
| Dependency versions | TBD |
| Compile command | TBD |
| Host test command | TBD |
| Upload/serial procedure | TBD; owner operates hardware by default |
| Sketch path | Proposed ESP32_Sleep_PoC/ESP32_Sleep_PoC.ino |

## Task tracker
States: NOT_STARTED, IN_PROGRESS, IMPLEMENTED_PENDING_VERIFICATION, VERIFIED, MERGED, BLOCKED. Do not mark MERGED until actual git history/owner confirms. Hardware evidence must be real. A user-confirmed waiver is separate from a pass.

| ID | State | Build | Hardware | Notes |
|---|---|---|---|---|
| T01 | NOT_STARTED | pending | pending | |
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
- Actual board/core/dependency versions and sensor behavior unknown until T01–T03. No microphone/radios/deep sleep/ULP in baseline.

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

## Verification evidence index
Add entries keyed V01..V14 with date, firmware commit/config, procedure, expected, observed evidence, pass/fail/pending/waived. Do not store fabricated serial logs as actual observations. V14 remains deferred.
