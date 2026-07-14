# EP-017: Add feature-flag-called event ingress

## Status and dependencies

- **State:** Externally blocked
- **Blocked by:** EP-003, EP-004, EP-007, and a production UnrealHog feature-flag evaluation subsystem
- **Blocks:** Full feature-flag event parity only; does not block EP-029 core ingress acceptance
- **Parity row:** `$feature_flag_called`

## Goal

When feature flags become available, route deduplicated flag access events through the standard `/batch` ingress pipeline.

## Required changes

- Add a bounded tracker keyed by effective distinct ID, flag key, and serialized value.
- On an eligible flag read, emit `$feature_flag_called` with key, response, payload metadata, request ID, and evaluation reason when available.
- Honor `bSendFeatureFlagEvent`, consent, reset, identity changes, and SDK shutdown.
- Reset deduplication at the same lifecycle points as Unity.

## Acceptance criteria

- Repeated reads of the same identity/key/value emit once; a changed identity or value can emit again.
- Disabled tracking and missing flag details are safe and do not affect flag evaluation.
- The event inherits all standard identity, session, profile, before-send, queue, and retry behavior.
- Tests use a fake flag result source and assert the exact available metadata fields.

## Exclusions

- Do not implement flag fetching, caching, evaluation, or `/flags` requests in this task.
- Do not start until a real feature-flag read path exists; a placeholder public API is not acceptable.

## Unity references

- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/FeatureFlags/FeatureFlagManager.cs` (`TrackFlagCalled`)
- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/FeatureFlags/FlagCalledTracker.cs`
