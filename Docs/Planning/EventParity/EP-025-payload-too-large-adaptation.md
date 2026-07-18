# EP-025: Adapt batch size after HTTP 413

## Status and dependencies

- **State:** Completed
- **Blocked by:** EP-023
- **Blocks:** EP-029
- **Parity row:** Payload-too-large recovery

## Goal

Recover from oversized batches by reducing local send and threshold sizes without mutating project configuration.

## Required changes

- Maintain queue-local adjusted `MaxBatchSize` and flush threshold initialized from validated settings.
- On each `413`, halve both values with a minimum of one.
- Retain the failed events and let EP-024 control the retry time.
- Use adjusted values for subsequent batch selection and automatic threshold checks.

## Acceptance criteria

- Configured `50/20` becomes `25/10`, then `12/5`, and never drops below `1/1`.
- The original settings object remains unchanged.
- A later retry sends smaller batches and can drain successfully without regenerating events.
- A `413` at batch size one retains the event and does not loop immediately.
- Tests combine fake `413` and success responses with injected time.
- Run `Scripts/ci-paths.sh` first if the required `CI` symlinks are missing from the worktree, then run `Scripts/run-windows-tests.sh`; the Unreal Automation tests must pass and their output must be recorded as a Zeroshot validation gate.
- When this task is completed, update its **State** to `Completed` and replace its status icon in `Docs/Planning/EventParity/README.md` with `✅`.

## Exclusions

- Do not estimate HTTP byte size before sending.
- Do not restore adjusted sizes during the same queue lifetime; reinitialization resets them from config.

## Unity references

- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/Core/EventQueue.cs` (`ShouldDeleteEventsOnError` 413 branch)
