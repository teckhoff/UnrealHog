# EP-024: Add transient delivery retry backoff

## Status and dependencies

- **State:** Ready
- **Blocked by:** EP-023
- **Blocks:** EP-028, EP-029
- **Parity row:** Queue retry pause after transient failure

## Goal

Pause repeated transient delivery attempts using Unity's deterministic linear backoff without busy loops or sleeps on the game thread.

## Required changes

- Track consecutive retryable failures and `PausedUntil` using an injectable UTC/monotonic clock.
- Use `min(failure_count * 5 seconds, 30 seconds)`.
- Timer, threshold, and manual flush attempts during the pause must not create HTTP requests.
- Reset retry state after a successful batch; retain it across unrelated enqueue operations.
- Schedule or allow the next normal timer/manual attempt after the pause without blocking.

## Acceptance criteria

- Deterministic tests assert delays of 5, 10, 15, 20, 25, and capped 30 seconds.
- Attempts immediately before/at the boundary behave consistently.
- Success clears the pause and the next transient failure starts again at 5 seconds.
- Permanent failures do not enter retry backoff.
- No test uses wall-clock sleeping.
- Run `Scripts/ci-paths.sh` first if the required `CI` symlinks are missing from the worktree, then run `Scripts/run-windows-tests.sh`; the Unreal Automation tests must pass and their output must be recorded as a Zeroshot validation gate.
- When this task is completed, update its **State** to `Completed` and replace its status icon in `Docs/Planning/EventParity/README.md` with `✅`.

## Exclusions

- Do not add jitter or unlimited exponential retry; match the Unity event queue.
- Do not persist retry counters across process restarts.

## Unity references

- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/Core/EventQueue.cs` (`RetryDelaySeconds`, `_pausedUntil`)
