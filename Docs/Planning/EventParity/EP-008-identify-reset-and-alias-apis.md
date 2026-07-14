# EP-008: Add identify, reset, and alias event APIs

## Status and dependencies

- **State:** Blocked
- **Blocked by:** EP-003, EP-004, EP-007
- **Blocks:** EP-029
- **Parity row:** `$identify`, `$create_alias`, and reset identity behavior

## Goal

Expose idiomatic Blueprint/C++ identity operations and emit Unity-compatible identity events through the shared capture path.

## Required changes

- Add `Identify`, `Reset`, `Alias`, and `GetDistinctId` to `UPostHogRuntimeSubsystem` with consistent Blueprint metadata.
- `Identify` must persist the known ID first, then emit `$identify` with the prior `$anon_distinct_id` only on first identification.
- Support optional `$set` and `$set_once` objects using EP-003 values.
- `Alias` must validate input and emit `$create_alias` with `alias`.
- `Reset` must return to anonymous state, honoring `bReuseAnonymousId`, clear groups, and start a new session once EP-009 is integrated.

## Acceptance criteria

- Blank IDs/aliases are safe no-ops with no event.
- First identify links the prior anonymous ID; repeated identify does not relink it.
- Reset creates or reuses the anonymous ID according to configuration and persists the result.
- Every generated event passes consent, enrichment, reserved-property, and queue rules.
- Windows Automation asserts exact event names, distinct IDs, nested properties, and restart state.

## Exclusions

- Feature-flag reload and person-property flag caching are outside event ingress.
- Group membership is EP-011.

## Unity references

- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/PostHogSDK.cs` (`IdentifyInternalAsync`, `ResetInternalAsync`, `AliasInternal`)
- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/Core/IdentityManager.cs`
