# EP-016: Add application lifecycle event production

## Status and dependencies

- **State:** Completed
- **Blocked by:** EP-003, EP-004, EP-009
- **Blocks:** EP-027, EP-029
- **Parity row:** Application Installed, Updated, Opened, and Backgrounded

## Goal

Track installation/version state and produce the Unity-observable lifecycle events without coupling event creation to shutdown transport behavior.

## Required changes

- Add a private lifecycle handler using Unreal application foreground/background/termination delegates.
- Persist last-seen application version and build under state key `lifecycle`.
- Emit `Application Installed`, `Application Updated`, `Application Opened`, and `Application Backgrounded` with Unity-equivalent property names.
- Prevent duplicate foreground/background events when overlapping Unreal delegates report the same transition.
- Honor `bCaptureApplicationLifecycleEvents` and consent; update lifecycle state only after permission.

## Acceptance criteria

- First permitted launch emits Installed then Opened and persists current version/build.
- A version/build change emits Updated with previous values then Opened.
- Foreground after background emits Opened with `from_background=true`; repeated identical signals do not duplicate events.
- Disabled or opted-out operation emits no lifecycle event and performs no event-state write.
- Tests inject application metadata and lifecycle transitions.
- Run `Scripts/ci-paths.sh` first if the required `CI` symlinks are missing from the worktree, then run `Scripts/run-windows-tests.sh`; the Unreal Automation tests must pass and their output must be recorded as a Zeroshot validation gate.

## Exclusions

- Queue flush/write draining on background and quit is EP-027.
- Do not add Unity-specific component lifecycle concepts.

## Unity references

- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/Core/LifecycleHandler.cs`
