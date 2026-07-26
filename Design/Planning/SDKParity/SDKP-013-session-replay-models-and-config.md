# SDKP-013: Add validated session-replay models and configuration

## Status and dependencies

- **State:** Completed
- **Blocked by:** None
- **Blocks:** SDKP-014 through SDKP-018
- **Parity row:** Replay settings and rrweb-compatible snapshot data

## Goal

Define stable private replay-event models and reject unusable replay configuration before any capture or transport collaborator is created.

## Required changes

- Validate every existing `FPostHogSessionReplayConfig` field at the runtime boundary in addition to editor clamps.
- Add private rrweb-style models for meta, full snapshot, pointer/touch, console-log plugin, and network plugin events.
- Add the `$snapshot` envelope model with UUIDv7, timestamp, effective distinct ID, `$session_id`, `$window_id`, `$snapshot_source`, SDK metadata, and snapshot data.
- Centralize millisecond event timestamps and JSON serialization so later capture sources cannot invent field shapes.

## Acceptance criteria

- Runtime validation rejects throttle below 0.1 seconds, JPEG quality outside 1–100, scale outside 0.1–1.0, and queue/flush values below one.
- Invalid replay configuration disables replay with one actionable diagnostic and creates no capture, queue, or HTTP object.
- Each supported rrweb event type serializes to stable nested JSON with integer timestamps and preserves non-ASCII text.
- The `$snapshot` envelope contains the effective current identity and session exactly once and uses `$window_id == $session_id`.
- Model tests use deterministic UUIDs/timestamps and JSON fixtures only.
- Run `Scripts/ci-paths.sh` first if required `CI` symlinks are missing, then run `Scripts/run-windows-tests.sh`; record the passing Unreal Automation output as the Zeroshot validation gate.
- When complete, set this task to `Completed` and replace its index status with `✅`.

## Exclusions

- Do not capture frames, listen to input/logs/network, or send `/s/` requests.
- Keep replay models private unless a later public API has a concrete need.

## Unity references

- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/SessionReplay/PostHogSessionReplayConfig.cs`
- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/SessionReplay/Models/RRWebModels.cs`
- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/SessionReplay/ReplayQueue.cs` (`SnapshotEvent`)

## Implementation notes

- `UPostHogDeveloperSettings::GetSessionReplayConfig` exposes the serialized struct verbatim; the editor clamps stay advisory and `PostHogSessionReplayConfigValidation::TryValidate` is the runtime boundary that decides whether replay may run.
- Validation rejects non-finite floats and out-of-enum `MinLogLevel` values in addition to the Unity range rules, because config files and reflection can deliver values the editor never produces.
- `PostHogSettingsValidation` validates replay only when it is enabled. Invalid replay leaves core analytics valid, exposes no `FPostHogValidatedSessionReplayConfig`, and replaces the generic SDKP-018 notice with one process-wide actionable diagnostic.
- `PostHogSessionReplayModels` owns every rrweb field shape, the single `FDateTime` to Unix-millisecond converter, and the single condensed JSON serializer, so later capture sources cannot invent their own.
- The `$snapshot` envelope factory takes injectable clock and UUID sources; the production overload uses UTC now plus a UUIDv7. `$window_id` always mirrors `$session_id`.
- No capture source, replay queue, or `/s/` transport is created by this change.

## Zeroshot validation

- Command: `Scripts/run-windows-tests.sh`
- Environment: WSL2 using repository `CI/UnrealEngine`, `CI/HostProject`, and `CI/Reports` links.
- Result:

```text
BUILD RESULT: PASS
AUTOMATION RESULT: 278 passed, 0 passed-with-warnings, 0 failed, 0 not run
AUTOMATION RESULT: PASS
```
