# EP-023: Classify delivery failures as permanent or retryable

## Status and dependencies

- **State:** Completed
- **Blocked by:** EP-022
- **Blocks:** EP-024, EP-025, EP-029
- **Parity row:** Unity `ShouldDeleteEventsOnError`

## Goal

Prevent permanent client errors from poisoning the queue while retaining events for failures that may recover.

## Required changes

- Add a pure private response classifier used by the flush state machine.
- Classify status `0`, redirects, `413`, and `5xx` as retryable.
- Classify `4xx` other than `413` as permanent and delete the attempted IDs.
- Preserve the default retryable treatment for unrecognized/no-status failures.
- Distinguish transport success from status classification in logs and test results.

## Acceptance criteria

- Focused table-driven tests cover `0`, representative `2xx`, all `3xx` boundaries, `400`, `401`, `404`, `413`, `429`, `499`, `500`, and `599`.
- Permanent failures delete only the attempted batch and end the current flush.
- Retryable failures retain the attempted and later records.
- A successful `2xx` retains existing success/deletion behavior.
- No live HTTP request is used.
- Run `Scripts/ci-paths.sh` first if the required `CI` symlinks are missing from the worktree, then run `Scripts/run-windows-tests.sh`; the Unreal Automation tests must pass and their output must be recorded as a Zeroshot validation gate.
- When this task is completed, update its **State** to `Completed` and replace its status icon in `Docs/Planning/EventParity/README.md` with `✅`.

## Exclusions

- `413` size reduction is EP-025; timed backoff is EP-024.
- Do not retry permanent errors a configurable number of times.

## Unity references

- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/Core/EventQueue.cs` (`ShouldDeleteEventsOnError`)
