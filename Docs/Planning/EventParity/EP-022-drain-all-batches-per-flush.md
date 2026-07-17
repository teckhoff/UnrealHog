# EP-022: Drain all available batches per flush

## Status and dependencies

- **State:** Completed
- **Blocked by:** EP-001, EP-019, EP-021
- **Blocks:** EP-023, EP-026, EP-027, EP-028, EP-029
- **Parity row:** Unity `FlushCoroutine` multi-batch behavior

## Goal

Make one flush operation continue sequentially until storage is empty, a request fails, cancellation occurs, or progress becomes impossible.

## Required changes

- Implement a private asynchronous flush state machine with one active request at a time.
- After each successful request, delete only its IDs and immediately schedule the next batch.
- Coalesce concurrent flush requests into the active operation rather than starting overlapping sends.
- Expose an internal completion result needed by manual and shutdown callers.
- Guard every transition against queue/subsystem destruction.

## Acceptance criteria

- A queue larger than `MaxBatchSize` drains in ordered batches during one flush.
- No event is sent concurrently, duplicated, or deleted before its successful response.
- A failure stops subsequent batches and leaves failed/later records persisted.
- Empty and already-flushing calls complete safely and do not create a request.
- Tests cover exact-boundary sizes, multiple batches, enqueue during flush, cancellation, and deletion failure.
- Run `Scripts/ci-paths.sh` first if the required `CI` symlinks are missing from the worktree, then run `Scripts/run-windows-tests.sh`; the Unreal Automation tests must pass and their output must be recorded as a Zeroshot validation gate.
- When this task is completed, update its **State** to `Completed` and replace its status icon in `Docs/Planning/EventParity/README.md` with `✅`.

## Exclusions

- Error classification, backoff, and 413 adaptation are EP-023–EP-025.
- Do not block the game thread waiting for HTTP.

## Unity references

- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/Core/EventQueue.cs` (`FlushCoroutine`)
