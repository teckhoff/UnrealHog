# SDKP-011: Integrate feature flags with consent and identity lifecycle

## Status and dependencies

- **State:** Blocked
- **Blocked by:** SDKP-009, SDKP-010, EP-007, EP-008, EP-011
- **Blocks:** EP-017, SDKP-012, UNREAL-003
- **Parity row:** Preload, identify, reset, groups, opt-in/out, and shutdown behavior

## Goal

Keep flag requests and cached values aligned with the effective identity and consent lifecycle.

## Required changes

- Lazily construct the feature-flag manager only after collection is permitted; load cache and optionally preload according to `bPreloadFeatureFlags`.
- Send effective distinct ID, optional anonymous ID, and current groups on every reload.
- During identify, merge `$set_once` and `$set` into person evaluation properties before switching identity, then reload for the identified user.
- On group membership changes, reload with the updated group map.
- On reset, clear flag response cache, evaluation-property state, and flag-call deduplication before reloading for the new anonymous identity.
- On opt-out or subsystem shutdown, cancel requests, clear consent-scoped flag state, and release callbacks safely.

## Acceptance criteria

- No flag identifier, cache artifact, request body, or HTTP request exists before consent.
- Preload happens once per permitted enable lifecycle when configured, and never when disabled.
- Identify requests use the new distinct ID and the merged profile properties; reset cannot expose the previous user's cached flags.
- Group updates affect the next request without racing the persisted membership update.
- Opt-out cancels in-flight work and suppresses late callbacks/events; re-opt-in creates one clean manager without duplicate delegates.
- `bReuseAnonymousId` controls whether the anonymous ID is included consistently with existing identity policy.
- Tests use fake identity, storage, and transport collaborators.
- Run `Scripts/ci-paths.sh` first if required `CI` symlinks are missing, then run `Scripts/run-windows-tests.sh`; record the passing Unreal Automation output as the Zeroshot validation gate.
- When complete, update EP-017's external dependency to this completed task, set this task to `Completed`, and replace its index status with `✅`.

## Exclusions

- Do not emit `$feature_flag_called`; EP-017 owns access tracking.
- Do not gate core capture on successful feature-flag loading.

## Unity references

- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/PostHogSDK.cs` (`Identify`, `Reset`, initialization and flag APIs)
- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/FeatureFlags/FeatureFlagManager.cs`
