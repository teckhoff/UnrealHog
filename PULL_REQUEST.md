# feat(settings): mark unavailable capabilities explicitly

- **Base:** `dev`
- **Head:** `zeroshot/indigo-arch-98`
- **Draft:** Yes
- **Assignee:** `Heinwald`

## Problem

Project Settings exposes feature-flag preload and session-replay configuration even though neither runtime capability exists yet. A developer can otherwise enable these fields and receive silent no-op behavior.

## Changes

- Mark feature-flag and session-replay settings as unavailable in editor metadata and disable editing until SDKP-012 and SDKP-018.
- Preserve every serialized property name and default for future compatibility.
- Add non-blocking validation diagnostics and emit at most one warning per unavailable capability family per process when collection is enabled.
- Add focused automation coverage for editor metadata, serialization compatibility, non-blocking validation, and warning deduplication.
- Repair the duplicate HTTP client constructor and normalize its host through the shared endpoint helper so the current `dev` baseline compiles.
- Mark SDKP-004 complete in the parity task and index.

## How did you test this code?

- **Command:** `Scripts/run-windows-tests.sh`
- **Environment:** WSL2 invoking Unreal Build Tool and Unreal Automation Tool on Windows through the repository `CI/UnrealEngine`, `CI/HostProject`, and `CI/Reports` links.
- **Result:** Passed
- **Relevant output:** `BUILD RESULT: PASS`; `AUTOMATION RESULT: 256 passed, 0 passed-with-warnings, 0 failed, 0 not run`; `AUTOMATION RESULT: PASS`.

The added tests catch regressions where unavailable settings become editable or lose their removal-task annotations, serialized names/defaults change, diagnostics block core analytics, or warnings repeat across opt-out and re-opt-in.

## Docs update

Updated `Design/Planning/SDKParity/SDKP-004-unavailable-capability-settings.md` with implementation notes, removal criteria, and the actual Zeroshot validation result. Updated the parity index status to completed.

## 🤖 Agent context

> This issue or pull request was created by an automated coding agent, not a human.

**Autonomy:** Human-driven (agent-assisted)

The implementation was generated with GPT-5.5. Codex using GPT-5.6-sol reviewed the patch against SDKP-004, verified the Unity parity references, ran the required Unreal gate, and authored this PR. The `github:yeet` publishing skill was used to scope, commit, push, and publish the change. No shareable agent-session link was available in this environment.

The reviewer should note that the HTTP constructor repair is an integration fix for a compile break already present on the current `dev` baseline; it does not add SDKP-004 behavior.
