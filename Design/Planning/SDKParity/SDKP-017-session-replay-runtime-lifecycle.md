# SDKP-017: Integrate the session-replay runtime lifecycle

## Status and dependencies

- **State:** Blocked
- **Blocked by:** SDKP-014 through SDKP-016, EP-002, EP-009, EP-013, EP-027
- **Blocks:** SDKP-018
- **Parity row:** Public replay control plus consent, session, screen, and application lifecycle

## Goal

Orchestrate session replay from `UPostHogRuntimeSubsystem` so capture is active only when configured, permitted, foregrounded, and supported.

## Required changes

- Add Blueprint/C++ `StartSessionReplay`, `StopSessionReplay`, `IsSessionReplayActive`, and replay flush behavior with observable success/failure semantics.
- Automatically start after opt-in when `bSessionReplay` is enabled and configuration/platform validation succeeds.
- Pause and best-effort flush on background; resume with a fresh meta event on foreground.
- On session rotation, discard old queued/pending replay data, reset capture throttle, and emit a new-session meta event.
- On opt-out, stop capture first, cancel transport, clear all replay/telemetry/input state, and release hooks.
- Make `CaptureScreen` update the replay screen name after its normal event validation.
- Stop and safely finalize replay during subsystem deinitialization and quit coordination.

## Acceptance criteria

- Before consent there are no capture delegates, readbacks, IDs, replay envelopes, buffers, timers, or `/s/` requests.
- Start/stop/pause/resume calls are idempotent and never duplicate hooks or timers.
- `IsSessionReplayActive` is true only while capture is running and not paused.
- Background, foreground, screen changes, session rotation, reset, opt-out/re-opt-in, and shutdown produce the documented state transitions.
- No envelope mixes snapshots from two identities or sessions; late readbacks from a prior generation are dropped.
- Core event capture and `/batch` flushing continue if replay is unsupported or fails.
- Tests use fake lifecycle, session, capture, and transport sources.
- Run `Scripts/ci-paths.sh` first if required `CI` symlinks are missing, then run `Scripts/run-windows-tests.sh`; record the passing Unreal Automation output as the Zeroshot validation gate.
- When complete, set this task to `Completed` and replace its index status with `✅`.

## Exclusions

- Do not expose private rrweb models or render resources publicly.
- Do not retain replay artifacts after opt-out for a later retry.
- Do not promise fatal-crash finalization.

## Unity references

- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/SessionReplay/SessionReplayIntegration.cs`
- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/PostHogSDK.cs` (replay lifecycle and public API)
