# SDKP-005: Add the exception person URL

## Status and dependencies

- **State:** Completed
- **Blocked by:** None
- **Blocks:** SDKP-021
- **Parity row:** `$exception_personURL` on manual and automatic exception events

## Goal

Attach PostHog's person-detail URL to every valid exception event using the effective identity and canonical project configuration.

## Required changes

- Build `$exception_personURL` as `<person-host>/project/<api-key>/person/<distinct-id>`.
- Derive the person host from the canonical ingest host by replacing `.i.` with `.` as the Unity SDK does.
- Add the property through shared exception composition so manual and automatic producers cannot diverge.
- Treat the property as SDK-owned so caller properties cannot spoof it.

## Acceptance criteria

- US, EU, and Custom host fixtures produce the expected person URL with no duplicate slash.
- Manual `CaptureException` and automatic nonfatal capture both attach the property once.
- The effective current `distinct_id` is used for anonymous and identified users; identity is not generated solely to build a URL before consent.
- Caller-provided `$exception_personURL` cannot replace the SDK value.
- Non-ASCII IDs and other path-sensitive values are encoded with one documented URL-path policy and covered by tests.
- Run `Scripts/ci-paths.sh` first if required `CI` symlinks are missing, then run `Scripts/run-windows-tests.sh`; record the passing Unreal Automation output as the Zeroshot validation gate.
- When complete, set this task to `Completed` and replace its index status with `✅`.

## Exclusions

- Do not add stack parsing, exception chains, or new automatic signal hooks.
- Do not change the existing event name or exception top-level fields.

## Unity references

- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/ErrorTracking/ExceptionManager.cs` (`$exception_personURL`)
