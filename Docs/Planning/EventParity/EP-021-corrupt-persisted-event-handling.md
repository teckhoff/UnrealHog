# EP-021: Remove corrupt persisted events without blocking ingress

## Status and dependencies

- **State:** Ready
- **Blocked by:** EP-018, EP-019
- **Blocks:** EP-022, EP-029
- **Parity row:** Stored-event load failure handling

## Goal

Prevent unreadable or malformed queue records from permanently blocking later valid events.

## Required changes

- During batch selection, delete records that cannot be loaded or fail EP-018 parsing.
- Continue selecting later IDs until the batch is full or no candidates remain.
- Log one diagnostic containing the event ID but not the full potentially sensitive payload.
- Handle delete failure explicitly and stop the current flush to avoid a tight loop.

## Acceptance criteria

- A corrupt first record is deleted and later valid records are sent in the same flush attempt.
- Missing files and malformed JSON are handled deterministically.
- A failed corrupt-record deletion ends the attempt without repeated immediate reads.
- Valid legacy events are never classified as corrupt solely because their UUID is not v7.
- Tests cover mixed valid/corrupt order and storage exceptions/failures.
- Run `Scripts/ci-paths.sh` first if the required `CI` symlinks are missing from the worktree, then run `Scripts/run-windows-tests.sh`; the Unreal Automation tests must pass and their output must be recorded as a Zeroshot validation gate.

## Exclusions

- Do not repair or rewrite malformed customer data.
- Do not log event bodies.

## Unity references

- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/Core/EventQueue.cs` (`LoadEvents`)
