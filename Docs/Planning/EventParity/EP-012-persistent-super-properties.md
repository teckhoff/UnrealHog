# EP-012: Add persistent super properties

## Status and dependencies

- **State:** Ready
- **Blocked by:** EP-003, EP-004
- **Blocks:** EP-029
- **Parity row:** Unity `Register` and `Unregister`

## Goal

Allow reusable properties to be registered once, persisted, and merged into every future event with Unity precedence.

## Required changes

- Add Blueprint/C++ register, unregister, and clear operations backed by storage state key `super_properties`.
- Load state only after collection permission initializes collaborators.
- Persist rich EP-003 values with a versioned or safely extensible JSON representation.
- Snapshot super properties during capture before merging call and SDK-owned values.

## Acceptance criteria

- Registered properties appear on later events and survive restart.
- Per-call values override matching super properties; SDK-owned values override both.
- Unregister/clear affects only future events and persists the removal.
- Blank keys and malformed stored JSON are safe and observable without blocking capture.
- Tests cover all rich value types and source-object independence.
- Run `Scripts/ci-paths.sh` first if the required `CI` symlinks are missing from the worktree, then run `Scripts/run-windows-tests.sh`; the Unreal Automation tests must pass and their output must be recorded as a Zeroshot validation gate.

## Exclusions

- Do not provide bulk UObject reflection registration.
- Do not mutate events already persisted.

## Unity references

- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/PostHogSDK.cs` (`RegisterInternal`, `UnregisterInternal`, `LoadSuperProperties`)
