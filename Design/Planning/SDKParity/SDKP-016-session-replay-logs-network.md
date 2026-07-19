# SDKP-016: Capture replay console logs and network telemetry

## Status and dependencies

- **State:** Blocked
- **Blocked by:** SDKP-013
- **Blocks:** SDKP-017, SDKP-018
- **Parity row:** Optional console and HTTP telemetry replay plugins

## Goal

Collect bounded, configurable diagnostic context for the next replay snapshot without recursively capturing UnrealHog's own activity.

## Required changes

- Add an Unreal log-output-device adapter that filters by `MinLogLevel`, excludes UnrealHog's category, truncates fields, and retains at most 100 entries.
- Add a network telemetry collector for method, URL, status, timing, and failure metadata with bounded storage and pause/resume behavior.
- Provide a public `RecordNetworkRequest` equivalent for requests the SDK cannot observe automatically.
- Convert drained entries into SDKP-013 console and network plugin events and clear only the drained snapshot of each buffer.

## Acceptance criteria

- `bCaptureLogs=false` and `bCaptureNetworkTelemetry=false` register no hooks and allocate no telemetry buffers.
- Log, warning, and error thresholds map predictably from Unreal verbosity; UnrealHog logs are excluded to prevent recursion.
- Message and stack/detail length caps, maximum entry counts, and oldest-entry eviction are covered by tests.
- Network samples preserve start/end timing and do not capture request/response bodies, credentials, or headers.
- Pause ignores new samples; resume starts cleanly; concurrent record/drain operations neither duplicate nor lose already-drained entries.
- Tests drive fake log and network sources only.
- Run `Scripts/ci-paths.sh` first if required `CI` symlinks are missing, then run `Scripts/run-windows-tests.sh`; record the passing Unreal Automation output as the Zeroshot validation gate.
- When complete, set this task to `Completed` and replace its index status with `✅`.

## Exclusions

- Do not globally intercept arbitrary game HTTP if Unreal provides no safe supported hook.
- Do not collect bodies, auth tokens, cookies, or arbitrary headers.
- Do not send plugin events independently of a replay snapshot.

## Unity references

- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/SessionReplay/ConsoleLogCapture.cs`
- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/SessionReplay/NetworkTelemetry.cs`
- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/SessionReplay/Models/RRWebModels.cs`
