# SDKP-015: Capture replay screenshots and pointer input

## Status and dependencies

- **State:** Blocked
- **Blocked by:** SDKP-013
- **Blocks:** SDKP-017, SDKP-018
- **Parity row:** Unreal frame snapshots and pointer/touch interaction

## Goal

Produce throttled, scaled JPEG snapshots and time-aligned pointer events through Unreal-native capture seams without blocking the game thread.

## Required changes

- Add an injectable screenshot-capture interface with a production Unreal implementation using supported asynchronous render readback.
- Apply configured scale and JPEG quality, retain original viewport dimensions for replay metadata, and base64-encode the compressed image.
- Enforce one capture in flight and `ThrottleDelaySeconds` between accepted captures.
- Capture mouse and touch start/end plus supported move events, converting viewport coordinates to the rrweb top-left coordinate system.
- Buffer pointer events until the next snapshot and bound the buffer against unbounded input growth.

## Acceptance criteria

- Unsupported readback platforms disable automatic capture with one diagnostic and no per-frame retries.
- A successful capture emits a meta event and full-snapshot event with matching timestamps and configured image dimensions/quality.
- Failed, empty, stale-session, or cancelled readbacks enqueue nothing and release in-flight state.
- Pointer coordinates, phase mapping, ordering, timestamps, and buffer clearing are deterministic in tests.
- Pause/stop prevents new readbacks and late callbacks cannot enqueue into a destroyed replay owner.
- Tests use a fake frame source and synthetic input; no Automation test reads the desktop or real player viewport.
- Run `Scripts/ci-paths.sh` first if required `CI` symlinks are missing, then run `Scripts/run-windows-tests.sh`; record the passing Unreal Automation output as the Zeroshot validation gate.
- When complete, set this task to `Completed` and replace its index status with `✅`.

## Exclusions

- Do not implement video capture, UI hierarchy reconstruction, masking rules not present in the reference, or synchronous GPU stalls.
- Do not enqueue or transport snapshots; SDKP-014 owns delivery.

## Unity references

- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/SessionReplay/ScreenshotCapture.cs`
- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/SessionReplay/SessionReplayIntegration.cs` (`RecordTouch`, capture loop)
- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/SessionReplay/Models/RRWebModels.cs`
