# SDKP-018: Add the isolated session-replay acceptance suite

## Status and dependencies

- **State:** Blocked
- **Blocked by:** SDKP-013 through SDKP-017
- **Blocks:** Declaring session-replay parity complete; removing the replay warning from SDKP-004
- **Parity row:** End-to-end replay without live credentials or real screen capture

## Goal

Prove replay capture, auxiliary telemetry, lifecycle, queueing, compression, and delivery as one observable consent-safe system.

## Required changes

- Build a private fixture with fake viewport/readback, input, logs, network samples, clock, reachability, identity/session state, lifecycle signals, compression, and replay transport.
- Drive public runtime-equivalent calls through snapshot composition and `/s/` request completion.
- Add a checklist mapping each assertion group to SDKP-013–SDKP-017.

## Acceptance criteria

- Denied consent produces zero hooks, buffers, IDs, readbacks, envelopes, timers, or requests.
- Opt-in produces a meta event; a fake frame plus pointer/log/network samples produces the exact expected rrweb-style snapshot data.
- Throttle, capture-in-flight, queue threshold/capacity, multi-batch drain, gzip fallback, permanent failure, backoff, adaptive 413, and offline retention are covered.
- Background/foreground, screen changes, session rotation, identity reset, opt-out, re-opt-in, and shutdown demonstrate isolation and cleanup.
- Unsupported screenshot capture disables replay without affecting core analytics.
- No test contacts a live host, captures a real viewport, or uses real credentials.
- Run `Scripts/ci-paths.sh` first if required `CI` symlinks are missing, then run `Scripts/run-windows-tests.sh`; record the passing Unreal Automation output as the Zeroshot validation gate.
- When complete, set this task to `Completed`, replace its index status with `✅`, and remove the replay unavailable treatment introduced by SDKP-004.

## Exclusions

- Do not duplicate JPEG codec, JSON primitive, session manager, or `/batch` queue unit tests.
- Do not require a real PostHog replay viewer as an acceptance dependency.

## Unity references

- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/SessionReplay/SessionReplayIntegration.cs`
- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/SessionReplay/ReplayQueue.cs`
- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/SessionReplay/ScreenshotCapture.cs`
- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/SessionReplay/ConsoleLogCapture.cs`
- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/SessionReplay/NetworkTelemetry.cs`
- `Design/Reference/posthog-unity/tests/PostHog.Unity.Tests/ReplayQueueTests.cs`
