# EP-018: Rehydrate persisted events without changing them

## Status and dependencies

- **State:** Ready
- **Blocked by:** None
- **Blocks:** EP-019, EP-021, EP-029
- **Parity row:** Persisted event deserialization

## Goal

Parse stored event JSON into a sendable event while preserving its original UUID, timestamp, identity, and complete property tree.

## Required changes

- Add a private parse/factory operation for `FPostHogEvent` or a private stored-event representation.
- Require correctly typed `uuid`, `event`, `distinct_id`, `timestamp`, and `properties` fields.
- Preserve nested objects, arrays, nulls, numbers, and legacy UUID versions exactly enough for semantic JSON round-trip.
- Never run enrichment, generate identifiers, or invoke before-send during rehydration.
- Return a diagnostic result rather than partially constructing an event.

## Acceptance criteria

- Valid current and legacy UUID event fixtures rehydrate with unchanged top-level values and property types.
- UUID filename/key and JSON UUID remain identical across load and batch projection.
- Missing, malformed, or incorrectly typed required fields fail without a generated replacement.
- Tests compare parsed JSON structure rather than key order or formatting.
- Windows Automation and platform-neutral fixture validation pass.
- Run `Scripts/ci-paths.sh` first if the required `CI` symlinks are missing from the worktree, then run `Scripts/run-windows-tests.sh`; the Unreal Automation tests must pass and their output must be recorded as a Zeroshot validation gate.

## Exclusions

- Do not delete corrupt records; EP-021 owns queue policy.
- Do not migrate or enrich old payloads.

## Unity references

- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/Core/EventQueue.cs` (`LoadEvents`, `DeserializeEvent`)
- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/Models/PostHogEvent.cs`
