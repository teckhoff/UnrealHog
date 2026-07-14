# EP-029: Add the isolated core ingress acceptance suite

## Status and dependencies

- **State:** Blocked
- **Blocked by:** EP-001 through EP-016 and EP-018 through EP-028
- **Blocks:** Declaring core `/batch` event ingress parity complete
- **Parity row:** End-to-end event ingress excluding feature-flag fetching and session replay

## Goal

Prove the completed event pipeline as one observable system without PostHog credentials or live network access.

## Required changes

- Build a private test fixture with fake storage, transport, clock, reachability, application metadata, lifecycle signals, and deterministic settings.
- Exercise public subsystem calls through event composition, persistence, batch serialization, responses, deletion/retry, and restart recovery.
- Add a parity checklist mapping every assertion to EP-001–EP-016 and EP-018–EP-028.
- Keep producer-specific unit tests in their owning tasks; this suite covers cross-component contracts only.

## Acceptance criteria

- Denied consent produces zero identifiers, records, files, and requests; opt-in enables the full path and opt-out clears it.
- Custom, identity, group, screen, exception, and lifecycle events carry correct distinct/session/profile/default properties.
- Super-property/call/SDK/before-send precedence is demonstrated.
- Restart recovery, capacity, corrupt records, multi-batch drain, permanent errors, backoff, adaptive 413, offline recovery, background, and timeout paths are covered.
- No test contacts a live host or uses real credentials; Windows build and the focused Unreal Automation suite pass and are recorded.

## Exclusions

- EP-017 remains separately blocked until a feature-flag subsystem exists.
- Do not duplicate low-level UUID, JSON, settings, or file-provider tests already owned elsewhere.

## Unity references

- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/PostHogSDK.cs`
- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/Core/EventQueue.cs`
- `Docs/Reference/posthog-unity/tests/PostHog.Unity.Tests/PostHogSDKTests.cs`

