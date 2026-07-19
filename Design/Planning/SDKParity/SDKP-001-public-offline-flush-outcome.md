# SDKP-001: Expose the offline manual-flush outcome

## Status and dependencies

- **State:** Completed
- **Blocked by:** None
- **Blocks:** Honest public manual-flush reporting
- **Parity row:** Public manual flush while reachability is known offline

## Goal

Let callers distinguish an empty queue from a queue deliberately preserved because the platform is offline.

## Required changes

- Add `SkippedOffline` to the public `EPostHogFlushOutcome` enum.
- Translate `EPostHogEventQueueFlushResult::SkippedOffline` explicitly in `UPostHogRuntimeSubsystem`.
- Preserve the existing immediate `EPostHogFlushRequestResult`; this task changes only the eventual completion result.
- Add subsystem-facing Automation coverage for an accepted manual flush that reaches the offline queue gate.

## Acceptance criteria

- A manual flush with queued events and known-offline reachability completes exactly once with `EPostHogFlushOutcome::SkippedOffline`.
- The same call preserves queued records and sends no HTTP request.
- An actually empty queue still reports `Empty`; all other public outcomes retain their current mappings.
- Blueprint enum metadata remains stable and the C++ delegate receives the new value without exposing private queue types.
- Run `Scripts/ci-paths.sh` first if required `CI` symlinks are missing, then run `Scripts/run-windows-tests.sh`; record the passing Unreal Automation output as the Zeroshot validation gate.
- When complete, set this task to `Completed` and replace its index status with `✅`.

## Exclusions

- Do not change reachability detection, retry behavior, or the immediate flush request result.
- Do not add data-loss behavior for offline queues.

## Unity references

- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/Core/EventQueue.cs`

## Validation

- Status recorded as `State: Completed`; index row uses `✅`.
- `rg -n "EPostHogEventQueueFlushResult|FPostHogEventQueue" UnrealHog/Source/UnrealHog/Public`: no matches.
- `[ -L CI/UnrealEngine ] || Scripts/ci-paths.sh`: passed.
- `Scripts/run-windows-tests.sh`: `BUILD RESULT: PASS`; `AUTOMATION RESULT: 247 passed, 0 passed-with-warnings, 0 failed, 0 not run`; `AUTOMATION RESULT: PASS`.
