# SDKP-007: Add a mockable feature-flag transport

## Status and dependencies

- **State:** Blocked
- **Blocked by:** SDKP-002, SDKP-006
- **Blocks:** SDKP-009, SDKP-010, SDKP-012
- **Parity row:** Consent-gated `POST /flags/?v=2`

## Goal

Fetch feature flags through a private, cancellable transport that can be verified without a live PostHog project.

## Required changes

- Add a private request type containing API key, effective distinct ID, optional anonymous ID, groups, person properties, and group properties.
- Send JSON to `POST <canonical-host>/flags/?v=2` with the SDK's JSON headers, User-Agent, and 10-second timeout.
- Add a mockable request/transport seam and cancellation-safe exactly-once completion.
- Honor `FeatureFlagRequestMaxRetries` as retries after the initial attempt, with deterministic exponential delays starting at 300 ms.
- Match the reference retry classification: retry selected status-zero transient connection failures and HTTP 502/504; do not retry other HTTP or data-processing failures.

## Acceptance criteria

- Request JSON omits absent optional fields and uses `$anon_distinct_id`, `$groups`, `person_properties`, and `group_properties` with stable nested JSON types when present.
- A configured retry count of zero sends once; `N` sends at most `N + 1` attempts.
- Reset/timeout/EOF/connection-lost status-zero failures and 502/504 follow the bounded retry path; connection refusal, 408, 429, 500, 503, and malformed-response failures do not.
- Concurrent cancellation or owner destruction cannot deliver a late callback into released state.
- Tests inspect fake requests and fake clock delays and never contact PostHog.
- No request object or payload is created before analytics collection is permitted.
- Run `Scripts/ci-paths.sh` first if required `CI` symlinks are missing, then run `Scripts/run-windows-tests.sh`; record the passing Unreal Automation output as the Zeroshot validation gate.
- When complete, set this task to `Completed` and replace its index status with `✅`.

## Exclusions

- Do not cache responses, expose flag reads, or emit flag-called events.
- Do not reuse `/batch` retry classification where it differs from the reference flag policy.

## Unity references

- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/Core/NetworkClient.cs` (`FetchFeatureFlags`, `CreateFlagsRequest`)
- `Design/Reference/posthog-unity/tests/PostHog.Unity.Tests/NetworkClientTests.cs`
