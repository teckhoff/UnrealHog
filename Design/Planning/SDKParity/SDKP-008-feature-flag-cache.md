# SDKP-008: Add the consent-safe feature-flag cache

## Status and dependencies

- **State:** Blocked
- **Blocked by:** SDKP-006, EP-002
- **Blocks:** SDKP-009, SDKP-010, SDKP-012
- **Parity row:** In-memory and persistent feature-flag availability

## Goal

Make the latest valid flags available from memory and across restarts without creating or reading flag artifacts before consent.

## Required changes

- Add a private thread-safe cache backed by `IPostHogStorageProvider` state after collection is permitted.
- Load the versioned cached response when the feature-flag manager is enabled and atomically replace it after a valid server response.
- Expose private lookups for value, payload, full details, keys, request ID, evaluated-at, and loaded state.
- Clear all values and persistent cache state on opt-out; clear quota-limited responses as defined by the reference.
- Recover safely from missing, malformed, or newer-version cache data without crashing or overwriting a valid in-memory response with invalid input.

## Acceptance criteria

- A valid persisted response is readable before the first new network response after opt-in.
- No cache read, file creation, state write, JSON payload, identifier generation, or HTTP request occurs before consent.
- A successful update is visible atomically and persists all SDKP-006 fields.
- A malformed response leaves the last valid cache intact; quota-limited data clears flag values deterministically.
- Opt-out removes persisted flag data and makes reads return the documented missing/default result.
- Tests use in-memory storage and simulated restarts only.
- Run `Scripts/ci-paths.sh` first if required `CI` symlinks are missing, then run `Scripts/run-windows-tests.sh`; record the passing Unreal Automation output as the Zeroshot validation gate.
- When complete, set this task to `Completed` and replace its index status with `✅`.

## Exclusions

- Do not expose public subsystem APIs or initiate network reloads.
- Do not load cached data merely because `UPostHogRuntimeSubsystem` exists while opted out.

## Unity references

- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/FeatureFlags/FlagCache.cs`
- `Design/Reference/posthog-unity/tests/PostHog.Unity.Tests/FlagCacheTests.cs`
