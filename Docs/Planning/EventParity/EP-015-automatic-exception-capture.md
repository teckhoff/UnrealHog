# EP-015: Add configured automatic exception capture

## Status and dependencies

- **State:** Blocked
- **Blocked by:** EP-014
- **Blocks:** EP-029
- **Parity row:** Unity automatic handled/unhandled exception integration

## Goal

Connect supported Unreal exception/error signals to EP-014 without duplicate subscriptions, recursion, or live crashes in tests.

## Required changes

- Identify the narrow Unreal 5.8 runtime delegates that can safely represent uncaught or logged exceptions on supported targets.
- Register only when `bCaptureExceptions` is enabled and honor `bCaptureExceptionsInEditor`.
- Apply `ExceptionDebounceIntervalMs` using an injectable monotonic clock.
- Unregister on opt-out and subsystem deinitialization; guard against capturing UnrealHog's own logging recursively.
- Adapt platform data into the EP-014 input struct.

## Acceptance criteria

- Enabled synthetic delegate input emits one `$exception`; disabled/editor-excluded input emits none.
- Reinitialization and repeated opt-in never duplicate callbacks.
- Debounce boundary tests are deterministic and separate distinct exceptions after the interval.
- Handler removal is proven before subsystem destruction.
- Windows build and Automation run against Unreal delegates or a private adapter seam, never by crashing the test process.

## Exclusions

- Do not promise capture for fatal failures where Unreal cannot execute plugin code safely.
- Do not add crash-report upload or session replay.

## Unity references

- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/ErrorTracking/ExceptionManager.cs`
- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/ErrorTracking/UnityExceptionIntegration.cs`
