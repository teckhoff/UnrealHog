# EP-002: Add the runtime consent lifecycle

## Status and dependencies

- **State:** Ready
- **Blocked by:** None
- **Blocks:** EP-004, EP-007, EP-009, EP-013–EP-017, EP-027, EP-029
- **Parity row:** Opt-in, opt-out, and no pre-consent side effects

## Goal

Keep the game-instance subsystem callable while ensuring identifiers, event payloads, queue files, and HTTP collaborators do not exist until collection is permitted.

## Required changes

- Let the subsystem instance exist even when analytics is disabled or configuration is unusable; initialization in that state must remain side-effect free.
- Add idiomatic Blueprint/C++ `SetAnalyticsOptIn(bool)` and a read-only consent query.
- Seed runtime permission from the config-backed `bAnalyticsEnabled`, whose default remains `false`.
- On opt-in, validate settings and lazily create runtime collaborators; on failure remain opted out and report the reason.
- On opt-out, block new capture first, cancel in-flight delivery, clear queued events, release runtime collaborators, and leave non-event state compatible with later opt-in.

## Acceptance criteria

- Before permission, capture and flush are safe no-ops and create no UUID, event JSON, queue record, file, or HTTP request.
- Opt-in with valid settings enables capture once; repeated calls are idempotent.
- Opt-out clears queued events and prevents new ones; re-opt-in creates a fresh runtime session without duplicating delegates or timers.
- Invalid configuration cannot be bypassed by runtime opt-in.
- Tests use injected collaborator factories/counters; Windows build and Automation execution are required.

## Exclusions

- Do not persist a per-user consent choice; project configuration seeds each game-instance runtime state.
- Do not implement identity or sessions in this task.

## Unity references

- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/PostHogSDK.cs` (`OptIn`, `OptOut`, `IsOptedOut`)
- `Docs/Reference/posthog-unity/tests/PostHog.Unity.Tests/PostHogSDKTests.cs`
