# SDKP-009: Add public feature-flag reads and reloads

## Status and dependencies

- **State:** Blocked
- **Blocked by:** SDKP-007, SDKP-008
- **Blocks:** SDKP-010, SDKP-011, SDKP-012
- **Parity row:** Feature-flag value, enabled state, payload, reload, and loaded callback

## Goal

Expose idiomatic Blueprint and C++ feature-flag reads over the real cache and transport, with explicit missing/default behavior.

## Required changes

- Add `GetFeatureFlag`, `IsFeatureEnabled`, payload access, `ReloadFeatureFlags`, and loaded-state queries to `UPostHogRuntimeSubsystem`.
- Return the SDKP-006 public value shape so Boolean, string variant, and missing values are unambiguous.
- Expose payload as JSON text and/or `UPostHogEventProperties` with documented parse-failure behavior.
- Add a Blueprint-assignable feature-flags-loaded event and C++ completion path.
- Coalesce concurrent reload calls into one request while completing every registered callback once.

## Acceptance criteria

- Cache reads are synchronous, do not initiate hidden network requests, and return documented defaults when unavailable or opted out.
- `IsFeatureEnabled` is true for Boolean true and nonempty variants, false for Boolean false, empty variants, and missing values unless a caller default applies.
- Payload access preserves null, scalar, object, and array JSON without lossy string coercion.
- Reload completion and the loaded event fire once after success or terminal failure; concurrent callers share the same in-flight request.
- Invalid/blank keys are safe and do not emit events or requests.
- Public reads do not emit `$feature_flag_called` until EP-017 owns that side effect.
- Run `Scripts/ci-paths.sh` first if required `CI` symlinks are missing, then run `Scripts/run-windows-tests.sh`; record the passing Unreal Automation output as the Zeroshot validation gate.
- When complete, set this task to `Completed` and replace its index status with `✅`.

## Exclusions

- Do not add local flag evaluation; values come from the PostHog response cache.
- Do not add identity or evaluation-property side effects in this task.

## Unity references

- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/FeatureFlags/FeatureFlagManager.cs`
- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/FeatureFlags/PostHogFeatureFlag.cs`
- `Design/Reference/posthog-unity/tests/PostHog.Unity.Tests/PostHogFeatureFlagTests.cs`
