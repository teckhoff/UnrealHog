# EP-002: Add the runtime consent lifecycle

## Status and dependencies

- **State:** Completed
- **Blocked by:** None
- **Blocks:** EP-004, EP-007, EP-009, EP-013–EP-017, EP-027, EP-029
- **Parity row:** Opt-in, opt-out, and no pre-consent side effects

## Goal

Keep the game-instance subsystem callable while ensuring identifiers, event payloads, queue files, and HTTP collaborators do not exist until collection is permitted.

## Required changes

- Let the subsystem instance exist even when analytics is opted out or configuration is unusable; initialization in that state must remain side-effect free.
- UPostHogDeveloperSettings need a property for allowing the developer to set the Default User Opt-In status, but the default value should be Opt-Out.
- Add idiomatic Blueprint/C++ `SetAnalyticsOptIn(bool)` and a read-only consent query.
- Developer Setting bAnalyticsEnabled is for the developer turning off analytics completely without completely disabling the plugin.
- To record the user's opt-in status, save it as a State in the PostHogStorageProvider, and load it when the subsystem is initialized.
- On opt-in, validate settings and lazily create runtime collaborators; on failure remain opted out and report the reason.
- On opt-out, block new capture first, cancel in-flight delivery, clear queued events, release runtime collaborators, and leave non-event state compatible with later opt-in.

## Acceptance criteria

- Before permission, capture and flush are safe no-ops and create no UUID, event JSON, queue record, file, or HTTP request.
- Opt-in with valid settings enables capture once; repeated calls are idempotent.
- Opt-out clears queued events and prevents new ones; re-opt-in creates a fresh runtime session without duplicating delegates or timers.
- Invalid configuration cannot be bypassed by runtime opt-in.
- Tests use injected collaborator factories/counters to verify lifecycle behavior without live services.
- Run `Scripts/ci-paths.sh` first if the required `CI` symlinks are missing from the worktree, then run `Scripts/run-windows-tests.sh`; the Unreal Automation tests must pass and their output must be recorded as a Zeroshot validation gate.

## Exclusions

- Do not implement identity or sessions in this task.

## Unity references

- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/PostHogSDK.cs` (`OptIn`, `OptOut`, `IsOptedOut`)
- `Design/Reference/posthog-unity/tests/PostHog.Unity.Tests/PostHogSDKTests.cs`
