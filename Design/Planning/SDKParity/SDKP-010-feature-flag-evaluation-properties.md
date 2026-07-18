# SDKP-010: Add feature-flag evaluation properties

## Status and dependencies

- **State:** Blocked
- **Blocked by:** SDKP-007, SDKP-008, SDKP-009
- **Blocks:** SDKP-011, SDKP-012
- **Parity row:** Person and group properties supplied to flag evaluation

## Goal

Let callers persistently customize the person and group properties sent with feature-flag requests.

## Required changes

- Add Blueprint/C++ set and reset APIs for person properties used by flags.
- Add set and reset APIs for all group properties and for one group type.
- Persist the two property maps under versioned storage state after consent.
- Support an explicit reload option on mutations; merge updates by key rather than replacing unrelated entries.
- When `bSendDefaultPersonPropertiesForFlags` is enabled, merge current SDK/device/app defaults beneath caller flag properties.

## Acceptance criteria

- Person and per-group-type objects retain null, Boolean, number, string, object, and array values across restart.
- Caller evaluation properties override same-key default properties; group maps remain partitioned by group type.
- Reset-one-group leaves other groups intact; reset-all and person reset remove their persistent state.
- `reload=false` performs no request; `reload=true` shares the SDKP-009 reload/coalescing path.
- No property state is read or written before consent, and opt-out clears it.
- Request-body tests assert exact nested `person_properties` and `group_properties` JSON.
- Run `Scripts/ci-paths.sh` first if required `CI` symlinks are missing, then run `Scripts/run-windows-tests.sh`; record the passing Unreal Automation output as the Zeroshot validation gate.
- When complete, set this task to `Completed` and replace its index status with `✅`.

## Exclusions

- Do not change event super properties or `$set` profile properties.
- Do not implement server-side or local flag evaluation.

## Unity references

- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/FeatureFlags/FeatureFlagManager.cs` (person/group property regions)
- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/PostHogSDK.cs`
