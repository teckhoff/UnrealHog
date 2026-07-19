# SDKP-006: Add feature-flag response and value models

## Status and dependencies

- **State:** Completed
- **Blocked by:** None
- **Blocks:** SDKP-007, SDKP-008, SDKP-012
- **Parity row:** `/flags/?v=2` response parsing and cached representation

## Goal

Represent boolean and multivariate flags, payloads, request metadata, and evaluation details without exposing raw JSON implementation details as the public API.

## Required changes

- Add private response models for v3 `featureFlags`/`featureFlagPayloads` and v4 `flags` data returned by API version 2.
- Preserve request ID, evaluated-at timestamp, quota-limited information, flag ID/version, payload, and evaluation reason.
- Define a Blueprint-friendly public flag value that distinguishes missing, Boolean, and string-variant states.
- Add lossless JSON serialization needed for persistent caching, including an explicit cache schema version.

## Acceptance criteria

- Boolean `true`/`false`, nonempty and empty string variants, missing values, nested payload JSON, and mixed v3/v4 responses parse deterministically.
- V4 details take precedence over legacy values for the same key while legacy-only responses remain usable.
- Variant values take precedence over an accompanying Boolean enabled field, matching the reference behavior.
- Unknown response fields are tolerated; malformed required shapes fail safely without replacing a valid cache.
- Quota-limited, request ID, evaluated-at, metadata, and reason fields survive a serialize/deserialize round trip.
- Tests use local JSON fixtures only.
- Run `Scripts/ci-paths.sh` first if required `CI` symlinks are missing, then run `Scripts/run-windows-tests.sh`; record the passing Unreal Automation output as the Zeroshot validation gate.
- When complete, set this task to `Completed` and replace its index status with `✅`.

## Exclusions

- Do not send requests, persist a cache, or emit `$feature_flag_called`.
- Do not expose private response metadata types unless callers need a stable Unreal API.

## Unity references

- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/FeatureFlags/Models/FeatureFlag.cs`
- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/FeatureFlags/Models/FeatureFlagsResponse.cs`
- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/FeatureFlags/PostHogFeatureFlag.cs`
- `Design/Reference/posthog-unity/tests/PostHog.Unity.Tests/FeatureFlagModelTests.cs`
- `Design/Reference/posthog-unity/tests/PostHog.Unity.Tests/PostHogFeatureFlagTests.cs`
