# SDKP-004: Make unavailable capability settings explicit

## Status and dependencies

- **State:** Ready
- **Blocked by:** None
- **Blocks:** Honest configuration surface while feature flags and replay are absent
- **Parity row:** Project Settings reflect actual runtime capability

## Goal

Prevent feature-flag and session-replay settings from silently implying that unimplemented runtime behavior exists.

## Required changes

- Mark the feature-flag and session-replay settings as unavailable in editor-facing metadata and documentation, or hide them until their owning subsystem is compiled in.
- If configuration can still enable an unavailable family, emit one explicit validation diagnostic and keep collection behavior deterministic.
- Keep serialized property names and defaults compatible so projects do not lose configuration before the features land.
- Define removal criteria for the temporary unavailable state: SDKP-012 for feature flags and SDKP-018 for session replay.

## Acceptance criteria

- A developer cannot enable replay or flag preload in Project Settings and receive silent no-op behavior.
- No warning is emitted repeatedly per tick, event, or settings access.
- Core analytics initialization and consent behavior remain unchanged.
- Existing config files continue to load without property loss or redirects.
- Tests verify the editor/runtime diagnostic behavior without requiring either future subsystem.
- Run `Scripts/ci-paths.sh` first if required `CI` symlinks are missing, then run `Scripts/run-windows-tests.sh`; record the passing Unreal Automation output as the Zeroshot validation gate.
- When complete, set this task to `Completed` and replace its index status with `✅`.

## Exclusions

- Do not implement feature flags or session replay in this task.
- Do not delete or rename the existing settings.
- Do not describe unavailable behavior as experimental if there is no runtime implementation.

## Unity references

- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/PostHogConfig.cs`
- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/PostHogSettings.cs`
