# SDKP-012: Add the isolated feature-flag acceptance suite

## Status and dependencies

- **State:** Blocked
- **Blocked by:** SDKP-006 through SDKP-011 and EP-017
- **Blocks:** Declaring feature-flag parity complete; removing the feature-flag warning from SDKP-004
- **Parity row:** End-to-end feature flags without live credentials

## Goal

Prove models, consent, persistence, transport, public reads, identity transitions, evaluation properties, and `$feature_flag_called` as one observable system.

## Required changes

- Build a private fixture with fake storage, transport, clock, identity/session state, application metadata, and deterministic settings.
- Drive public subsystem-equivalent calls through request construction, response parsing, cache persistence, reads, reload callbacks, identity changes, and standard event ingress.
- Add a checklist mapping every assertion group to SDKP-006–SDKP-011 and EP-017.

## Acceptance criteria

- Denied consent produces zero IDs, cache reads/writes, payloads, requests, or flag-called events.
- Cached flags are immediately readable after permitted restart; a server response atomically replaces them and survives another restart.
- Boolean, variant, payload, v4 metadata, defaults, loaded callbacks, bounded retries, and malformed responses are covered.
- Person/group properties and SDK defaults appear with correct precedence in the request.
- Identify, group, reset, opt-out, and re-opt-in demonstrate correct request identity and state isolation.
- Repeated eligible reads exercise EP-017 deduplication and include available request/evaluation metadata through `/batch` ingress.
- No test contacts a live host or uses real credentials.
- Run `Scripts/ci-paths.sh` first if required `CI` symlinks are missing, then run `Scripts/run-windows-tests.sh`; record the passing Unreal Automation output as the Zeroshot validation gate.
- When complete, set this task to `Completed`, replace its index status with `✅`, and remove the feature-flag unavailable treatment introduced by SDKP-004.

## Exclusions

- Do not duplicate low-level JSON, storage-provider, identity, or event-queue tests.
- Do not include session replay or gameplay-feature gating.

## Unity references

- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/FeatureFlags/FeatureFlagManager.cs`
- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/FeatureFlags/FlagCache.cs`
- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/FeatureFlags/FlagCalledTracker.cs`
- `Design/Reference/posthog-unity/tests/PostHog.Unity.Tests/FeatureFlagModelTests.cs`
- `Design/Reference/posthog-unity/tests/PostHog.Unity.Tests/FlagCacheTests.cs`
- `Design/Reference/posthog-unity/tests/PostHog.Unity.Tests/FlagCalledTrackerTests.cs`
- `Design/Reference/posthog-unity/tests/PostHog.Unity.Tests/NetworkClientTests.cs`
- `Design/Reference/posthog-unity/tests/PostHog.Unity.Tests/PostHogFeatureFlagTests.cs`
