# EP-027: Flush safely on background and shutdown

## Status and dependencies

- **State:** Blocked
- **Blocked by:** EP-002, EP-016, EP-022, EP-026
- **Blocks:** EP-029
- **Parity row:** Background persistence and `FlushOnQuit`

## Goal

Preserve queued data when the application is suspended and attempt a bounded final network drain when it quits.

## Required changes

- On background, stop session activity, request an asynchronous flush, and synchronously drain pending event file writes before suspension.
- When `bFlushOnQuit` is enabled, initiate EP-022 drain and delay quit only through an Unreal-supported bounded lifecycle mechanism up to `FlushOnQuitTimeoutSeconds`.
- On timeout, retain unsent records, cancel safely, drain pending storage writes, and allow quit.
- When disabled, skip the network wait but still ensure queued writes are durable before collaborator destruction.
- Make deinitialization idempotent after either normal completion or timeout.

## Acceptance criteria

- Background and quit signals never delete unsent events merely because a request is cancelled.
- Successful final flush drains every batch before completion.
- Timeout permits shutdown with unsent files recoverable by EP-019 on next launch.
- Repeated/overlapping lifecycle signals invoke finalization once.
- Tests use fake lifecycle signals, transport, clock/timer, and storage drain counters; Windows verifies the real delegate wiring.
- Run `Scripts/ci-paths.sh` first if the required `CI` symlinks are missing from the worktree, then run `Scripts/run-windows-tests.sh`; the Unreal Automation tests must pass and their output must be recorded as a Zeroshot validation gate.
- When this task is completed, update its **State** to `Completed` and replace its status icon in `Docs/Planning/EventParity/README.md` with `✅`.

## Exclusions

- Do not guarantee network delivery after the configured timeout.

## Unity references

- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/Core/LifecycleHandler.cs` (`OnWantsToQuit`)
- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/PostHogSDK.cs` (`OnAppBackground`, `OnAppQuit`)
