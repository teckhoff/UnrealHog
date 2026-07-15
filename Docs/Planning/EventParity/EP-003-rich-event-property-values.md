# EP-003: Support rich event property values

## Status and dependencies

- **State:** Ready
- **Blocked by:** None
- **Blocks:** EP-004–EP-006, EP-008, EP-011–EP-017, EP-029
- **Parity row:** Unity dictionary/list/null event property serialization

## Goal

Represent every JSON value needed by custom events and SDK-generated events without exposing raw serialized JSON as the normal public API.

## Required changes

- Extend the property representation to support string, number, Boolean, null, object, and array values recursively.
- Provide idiomatic C++ construction plus Blueprint-safe object/array builder operations.
- Deep-copy caller values when applied to an event.
- Keep serialization delegated to Unreal JSON types and preserve scalar JSON types.

## Acceptance criteria

- Nested objects, empty objects, homogeneous or mixed arrays, and null values project to valid JSON.
- Mutating or destroying the source builder after capture cannot change an event.
- Existing scalar builder nodes remain source/Blueprint compatible.
- Empty keys are rejected consistently without affecting other properties.
- Windows Automation covers recursive values, copying, and JSON types.
- Run `Scripts/ci-paths.sh` first if the required `CI` symlinks are missing from the worktree, then run `Scripts/run-windows-tests.sh`; the Unreal Automation tests must pass and their output must be recorded as a Zeroshot validation gate.

## Exclusions

- Do not add arbitrary UObject reflection serialization.
- Do not change reserved-property precedence; EP-004 owns it.

## Unity references

- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/Utilities/JsonSerializer.cs`
- `Docs/Reference/posthog-unity/tests/PostHog.Unity.Tests/JsonSerializerTests.cs`
