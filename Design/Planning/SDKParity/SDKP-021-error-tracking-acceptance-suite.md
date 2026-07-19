# SDKP-021: Add the error-tracking phase-two acceptance suite

## Status and dependencies

- **State:** Blocked
- **Blocked by:** SDKP-005, SDKP-019, SDKP-020
- **Blocks:** Declaring broader nonfatal error-tracking parity complete
- **Parity row:** Person URL, rich exception lists, and automatic signal policy

## Goal

Prove manual and automatic exception behavior through one consent-safe fixture without process crashes or live services.

## Required changes

- Build a private fixture with fake clock, identity, settings, automatic-signal adapters, storage, and batch transport.
- Exercise public manual capture and every SDKP-020 source through standard event composition and `/batch` serialization.
- Add a checklist mapping every assertion group to EP-014, EP-015, SDKP-005, SDKP-019, and SDKP-020.

## Acceptance criteria

- Denied consent creates no person URL, exception payload, queue record, file, or request.
- Manual handled and automatic handled/unhandled cases produce the documented mechanism fields and one SDK-owned person URL.
- Single, chained, aggregate, cyclic, malformed-stack, capped-depth/count/frame, Unicode, and reserved-property cases are covered.
- Automatic registration, debounce, editor gating, recursion suppression, concurrent signal handling, opt-out, re-opt-in, and shutdown are covered.
- Every event inherits identity, session, profile, before-send, persistence, and retry behavior from normal ingress.
- Tests use fake signals and transport, never deliberately ensure/crash the test process, and never contact PostHog.
- Run `Scripts/ci-paths.sh` first if required `CI` symlinks are missing, then run `Scripts/run-windows-tests.sh`; record the passing Unreal Automation output as the Zeroshot validation gate.
- When complete, set this task to `Completed` and replace its index status with `✅`.

## Exclusions

- Do not duplicate event-queue retry tests or claim fatal-crash delivery.
- Do not require external symbolication or a PostHog error-tracking UI.

## Unity references

- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/ErrorTracking/ExceptionManager.cs`
- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/ErrorTracking/ExceptionPropertiesBuilder.cs`
- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/ErrorTracking/UnityExceptionIntegration.cs`
- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/ErrorTracking/WebGLExceptionIntegration.cs`
