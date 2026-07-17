# EP-011: Add group membership and group-identify events

## Status and dependencies

- **State:** Completed
- **Blocked by:** EP-003, EP-004, EP-007
- **Blocks:** EP-029
- **Parity row:** `$groups` and `$groupidentify`

## Goal

Persist group membership with identity and emit Unity-compatible group profile events through normal ingress.

## Required changes

- Add public Blueprint/C++ `Group` and group-clear operations with validated group type and key.
- Persist group membership through EP-007 identity state.
- Add the complete group map as SDK-owned `$groups` on subsequent events.
- Emit `$groupidentify` with `$group_type`, `$group_key`, and optional nested `$group_set`.
- Deep-copy returned and captured group maps so callers cannot mutate manager state.

## Acceptance criteria

- Blank group type/key changes no state and emits no event.
- Membership survives restart and is cleared by identity reset.
- A group event contains its new membership in `$groups` and the expected group-identify fields.
- Updating one group type does not remove other memberships.
- Windows Automation covers persistence, replacement, clearing, nested properties, and reserved precedence.
- Run `Scripts/ci-paths.sh` first if the required `CI` symlinks are missing from the worktree, then run `Scripts/run-windows-tests.sh`; the Unreal Automation tests must pass and their output must be recorded as a Zeroshot validation gate.

## Exclusions

- Group properties used only for feature-flag evaluation are outside event ingress.
- Do not implement feature-flag fetching.

## Unity references

- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/PostHogSDK.cs` (`GroupInternal`)
- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/Core/IdentityManager.cs`
