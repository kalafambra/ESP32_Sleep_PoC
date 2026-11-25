# AGENTS.md — ESP32 Sleep Mode PoC

## Project context
This repository implements an Arduino C++ proof of concept on the original ESP32-WROOM-32D: autonomous MPU6050 impact interrupts, slow/fast inclination monitoring, periodic BME280 environmental measurements, and explicit ESP32 light sleep. Minimize average current without sacrificing the agreed functionality. The owner tests on a physical board; no physical measurements are implied by a successful compilation.

## Sources of truth
- `docs/REQUIREMENTS.md`: normative requirements, configuration defaults, architecture, verification and limitations (Rev 0.4).
- `docs/TASKS.md`: ordered work packages, task IDs, scope, dependencies and merge gates.
- `docs/STATUS.md`: actual task progress, environment, decisions, test evidence and handoff. Initially nothing is implemented.
- `docs/WORKFLOW.md`: human workflow and reusable prompts.
- If code, task text and requirements disagree, report the conflict before changing behavior; requirements take precedence over illustrative code. Do not silently change requirements. New user-approved decisions must be recorded in the appropriate document.

## Session contract
1. Check current branch and `git status --short`; read REQUIREMENTS, STATUS and the selected task section in TASKS. Inspect relevant existing code/tests, not unrelated files. Identify the task by the user's ID or exact branch mapping. If neither is unambiguous, ask which task to implement. Do not assume the next task is authorized.
2. Work only on the selected task, on its feature branch. If on main or the wrong branch, stop before editing and ask the owner to switch. Do not overwrite unrelated dirty changes. Dependencies must be merged/verified or explicitly waived by the owner.
3. Briefly state the plan and tests, then implement the smallest coherent change. Use C++/Arduino APIs compatible with the installed Arduino-ESP32 core; do not introduce PlatformIO or a new framework. Keep config centralized; no hidden timing constants. Preserve previously working behaviors.
4. Compile using the recorded board FQBN/core and pinned dependencies if available. Prefer deterministic host tests for pure logic (inject time and fake I/O) and real board tests for GPIO/sensors/sleep. Do not invent toolchain versions, serial ports, hardware observations or test results. If tooling is missing, report exact manual instructions. No automatic upload or serial-port opening without owner permission.
5. Update STATUS with changes, commands/results, manual test instructions and unresolved risks. Mark implementation done but hardware verification pending until actual evidence is supplied. Never mark a test passed based on predicted logs. Do not automatically start another task.
6. Review diff and stop with concise handoff: summary, files, tests actually run, expected hardware observations, blockers, merge readiness. The owner controls commits, branch switching, merges, rebases, pushes and destructive operations; do none unless explicitly requested. Never merge into main yourself by default.

## Non-negotiable engineering rules
- Fixed wiring: I2C SDA21/SCL22; MPU INT GPIO4 active HIGH latched; BOOT GPIO0 active LOW; LED GPIO2 active HIGH or -1; 3.3 V sensors. No microphone, Wi-Fi/Bluetooth, deep sleep or ULP in baseline.
- One cooperative scheduler; no blocking delays/wait loops in armed state, no concurrent I2C access, no unconditional serial logging of timer wake-ups. Use monotonic 64-bit uptime/deadlines, check all pending work after wake. Disarm or safe fault on unrecoverable monitoring failures; no silent degraded armed state.
- Tilt: 1 s checks, 5 s persistence, 30° threshold, 3° hysteresis, gravity-vector inclination (no yaw), fixed reference. Environment: 10 s period, four non-blocking forced conversions. Impact: autonomous continuously enabled hardware motion detection while armed; no guaranteed classification/count of every physical tap.
- Motion interrupt ownership belongs to MotionSensor: explicit INT_STATUS read acknowledges latch; unrelated reads must not clear it. Service during awake and sleep operation. No I2C or Serial from ISR. Avoid asserted-pin wake storms, held-button retriggers and invalid timing on missed deadlines.
- Serial output must be complete and human-readable; diagnostic timestamps are recognition times. No claims of measured current without instruments. Keep tests and debug hooks removable or disabled by default in release-like operation.
