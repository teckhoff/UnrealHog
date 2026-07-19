# SDKP-004: Make unavailable capability settings explicit

## Status and dependencies

- **State:** Completed
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

## Implementation notes

- Feature-flag preload and related feature-flag settings remain serialized with their existing names and defaults, but Project Settings marks them unavailable and disables editing until SDKP-012.
- Session replay and `FPostHogSessionReplayConfig` remain serialized with their existing names and defaults, but Project Settings marks them unavailable and disables editing until SDKP-018.
- Runtime validation reports unavailable feature-flag preload or session replay as diagnostics only; valid analytics config remains valid and collection behavior is unchanged.
- Diagnostics are logged at most once per unavailable family per process when collection is enabled.

## Removal criteria

- Remove the SDKP-012 feature-flag unavailable metadata, edit lock, and diagnostic when feature-flag preload is implemented and covered by SDKP-012.
- Remove the SDKP-018 session-replay unavailable metadata, edit lock, and diagnostic when session replay is implemented and covered by SDKP-018.

## Zeroshot validation

- Command: `Scripts/run-windows-tests.sh`
- Environment: WSL2 using repository `CI/UnrealEngine`, `CI/HostProject`, and `CI/Reports` links.
- Result:

```text
BUILD RESULT: PASS
AUTOMATION RESULT: 256 passed, 0 passed-with-warnings, 0 failed, 0 not run
AUTOMATION RESULT: PASS
```
