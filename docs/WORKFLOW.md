# Working with Codex in VS Code

## Setup (once)
1. Install official OpenAI Codex extension for VS Code; sign in using your ChatGPT Plus account. Local IDE workflow is appropriate for the connected ESP32. Plan usage limits apply; no API key is required for ChatGPT-account sign-in. See https://help.openai.com/en/articles/11369540-using-codex-with-your-chatgpt-plan and https://developers.openai.com/learn/codex.
2. Open the actual Git repository root in VS Code, not merely the sketch subfolder. Extract this starter pack into root so AGENTS.md and docs/ are adjacent to .git. Do not overwrite existing project files blindly. Review/commit documentation on main before the first task. Codex recognizes AGENTS.md as repository guidance; keep it small and do not duplicate the entire spec there. See https://developers.openai.com/cookbook/examples/codex/iterating-development-workflows-with-codex.
3. Keep Arduino IDE for board manager, libraries, flashing and Serial Monitor; optionally install Arduino CLI for Codex to compile locally. Only one program should hold the serial port at a time. Record actual board FQBN/core/library versions in STATUS. Do not switch to PlatformIO unless intentionally changing requirements. Use local/workspace-scoped permissions; approve outside-workspace or network access only when needed; no unrestricted machine access required.

## Per task cycle
- Start from clean, updated main. If a remote exists, use `git pull --ff-only` on main after checking status; skip if local-only. Create feature branch yourself using exact TASKS.md mapping. Open a new Codex chat in local repository after branch switch. Name conversation with task ID. One branch per task, not necessarily one message per task.
- Use implementation prompt below; optionally plan mode for complex T07/T08 and review plan before allowing edits. For small tasks direct implementation is fine. Codex reads AGENTS and relevant docs, edits, runs compile/host tests, and updates STATUS. Keep same chat for same feature, hardware logs, fixes and follow-up verification. Do not start a new task while current branch unfinished unless you explicitly park it.
- Review changed files and `git diff`; compile and flash as needed. Owner performs physical tests and sends actual serial output and exact actions back to same chat. A successful build is not a hardware pass. Ask Codex to address findings, rerun relevant tests and record evidence. Do not let it fabricate or infer measurements.
- Before merge use review prompt (same chat for trivial work, fresh read-only review chat on same branch for critical T03/T05/T07/T08 if useful). Fix issues, run tests again, update STATUS. Commit when feature is coherent. Merge only when gates pass or an explicit documented waiver exists. Keep main buildable. One feature branch can have multiple commits; squashing at merge is optional. Use git CLI or VS Code Source Control; do not let Codex merge automatically by default.
- Start next task on new branch from merged main in new chat. No need to transfer previous chat summaries; code and STATUS provide handoff. If a new chat is required for same task due to context length, keep same branch and ask it to continue that task after reading STATUS/diff. If unrelated bug, separate fix branch from main after current feature is settled or park work without mixing. For parallel work, separate git worktrees and separate IDE windows, but sequential is simplest because tasks depend on each other and hardware is shared.
- When resuming T08, use the [remaining board checklist](STATUS.md#remaining-owner-board-verification). When starting T09 after T08 merges, read the [BME280 failure evidence](STATUS.md#intermittent-bme280-invalid-sample) and the [T09 work package](TASKS.md#t09--serial-diagnostics-and-fault-hardening).

## Example Git commands
First time (existing repository; review actual staged files, do not commit other work):
```sh
git status --short
git add AGENTS.md docs/REQUIREMENTS.md docs/TASKS.md docs/STATUS.md docs/WORKFLOW.md
git commit -m "docs: establish ESP32 PoC requirements and Codex plan"
```
Start T01 (main already exists and is clean):
```sh
git switch main
# git pull --ff-only    # only if remote configured
git switch -c feat/T01-foundation
```
After implementation/verification, review then stage intended files and commit (use actual paths rather than blindly staging unrelated changes):
```sh
git status --short
git diff --check
git diff
# git add <reviewed files>
git diff --cached
git commit -m "T01: establish Arduino project foundation"
git switch main
git merge --no-ff feat/T01-foundation
# confirm build/history and clean status, then optional branch cleanup
git branch -d feat/T01-foundation
```
For later tasks substitute branch and task ID. Never force-push, reset --hard, or delete unmerged work to resolve a conflict. If main changes between branch creation and merge, review/test integration before merging. A GitHub pull request is optional, not necessary for a solo local project. When using a PR, merge only after tests/review and sync local main.

## Copy/paste prompts
### First message in each new task chat
```text
Implement T01 only, as defined in docs/TASKS.md.
Use AGENTS.md and docs/REQUIREMENTS.md as the project contract; read docs/STATUS.md for the current baseline. Confirm current branch, working tree and dependencies before editing. Briefly state the plan and intended checks, then implement this task and run the available build/tests. Keep existing behavior intact and do not start subsequent tasks. Update STATUS with actual results, manual hardware test steps and blockers. Do not commit, switch branches, merge, push or upload without my approval. Stop with a concise reviewable handoff.
```
Change T01 to the selected ID. For a planning-only session, replace implementation instruction with “Propose a plan for T07 only; do not edit files yet.” Once reviewed, say “Implement the approved T07 plan.”

### Hardware feedback (same chat)
```text
I flashed the current branch with [board/core/config]. I performed [exact actions]. Expected [result]. Actual [result]. Serial log below is from the board:
[paste log]
Diagnose the discrepancy within the current task, make a minimal fix if justified, rerun applicable checks, and update docs/STATUS.md. Do not mark hardware tests passed until I confirm.
```

### Pre-merge review (same or separate read-only chat)
```text
Review the current branch for T03 against main, AGENTS.md, REQUIREMENTS.md and the T03 merge gate. Read the actual diff including uncommitted changes. Focus on behavioral bugs, races, power regressions, timing and missing evidence. Return findings with severity and file/line locations, plus whether it is merge-ready. Do not edit, commit or merge.
```
Change ID. After findings, return to implementation chat to fix, then verify again. Record the owner's actual hardware verdict in STATUS before committing.

### Resume same feature in a replacement chat
```text
Continue T08 on its existing branch, not a new task. Read STATUS and current git diff/history; identify what is implemented, tested and pending. Preserve all work. Address only the outstanding T08 items and update handoff.
```

### When requirements change
```text
I want to change [requirement ID] from [old value] to [new value]. First identify affected tasks, config and verification. Propose exact requirements/task updates; do not change firmware until I approve the scope.
```
Keep normative changes in REQUIREMENTS rather than scattered chat instructions. If already merged functionality changes, use a new scoped task/fix branch. Do not silently edit previously accepted behavior.

## Merge definition of done
A task is ready when the selected scope is complete, diff is understandable, build and relevant tests pass, hardware observations are supplied where required (or owner explicitly records a waiver), no known blocking regressions, and STATUS contains a usable handoff. The next task starts from merged main. Do not equate a chat ending or Codex saying “done” with verification. Optimize tokens by giving task ID + logs instead of repeating context; use fresh chats at feature boundaries and keep hardware debugging in the same chat.
