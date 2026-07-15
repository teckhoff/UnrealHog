# EP-026: Expose a public manual flush API

## Status and dependencies

- **State:** Blocked
- **Blocked by:** EP-022
- **Blocks:** EP-027, EP-029
- **Parity row:** Unity public `Flush`

## Goal

Allow Blueprint and C++ callers to request a complete asynchronous queue drain safely.

## Required changes

- Promote the subsystem flush operation to a clearly named Blueprint/C++ API.
- Return immediate request acceptance through a Blueprint-friendly result and offer a C++ completion delegate for full drain outcome.
- Route timer and public calls through the same EP-022 state machine.
- Treat uninitialized, opted-out, empty, already-flushing, and deinitializing states deterministically.

## Acceptance criteria

- One public call drains multiple batches when delivery succeeds.
- Concurrent calls do not create parallel requests; every registered C++ completion is resolved exactly once.
- Calls before consent or with an empty queue are safe and create no request.
- Blueprint metadata and logging match related subsystem APIs.
- Windows Automation covers acceptance/result behavior with fake transport.
- Run `Scripts/ci-paths.sh` first if the required `CI` symlinks are missing from the worktree, then run `Scripts/run-windows-tests.sh`; the Unreal Automation tests must pass and their output must be recorded as a Zeroshot validation gate.

## Exclusions

- Do not synchronously block Blueprint or the game thread.
- Final quit timeout behavior is EP-027.

## Unity references

- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/PostHogSDK.cs` (`Flush`)
