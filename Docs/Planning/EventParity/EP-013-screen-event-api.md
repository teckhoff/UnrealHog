# EP-013: Add the screen event API

## Status and dependencies

- **State:** Blocked
- **Blocked by:** EP-002, EP-003, EP-004
- **Blocks:** EP-029
- **Parity row:** Unity `Screen`

## Goal

Expose a focused screen-view producer that enters the same consent, composition, persistence, and transport pipeline as custom events.

## Required changes

- Add Blueprint/C++ `CaptureScreen` with a screen name and optional properties.
- Emit event `$screen` and set SDK-owned `$screen_name` from the explicit argument.
- Route through the central capture path without duplicating enrichment or queue logic.

## Acceptance criteria

- A valid call emits exactly one `$screen` event with `$screen_name` and caller properties.
- Caller input cannot replace `$screen_name` before the optional EP-006 interceptor.
- Blank screen names are rejected before event creation.
- Calls before consent or after opt-out create no event side effects.
- Windows Automation covers null/populated properties and consent gating.
- Run `Scripts/ci-paths.sh` first if the required `CI` symlinks are missing from the worktree, then run `Scripts/run-windows-tests.sh`; the Unreal Automation tests must pass and their output must be recorded as a Zeroshot validation gate.

## Exclusions

- Do not integrate session-replay screen naming.
- Do not automatically infer Unreal map or widget names.

## Unity references

- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/PostHogSDK.cs` (`Screen`)
