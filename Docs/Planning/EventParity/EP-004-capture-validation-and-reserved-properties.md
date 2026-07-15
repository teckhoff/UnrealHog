# EP-004: Enforce capture validation and reserved-property precedence

## Status and dependencies

- **State:** Blocked
- **Blocked by:** EP-002, EP-003
- **Blocks:** EP-006, EP-008, EP-010–EP-017, EP-029
- **Parity row:** `CaptureInternal` input validation and property merge order

## Goal

Create a single capture-composition path that rejects unusable event names and prevents caller input from overriding SDK-owned fields.

## Required changes

- Reject empty or whitespace-only event names before UUID creation or property composition.
- Centralize property composition in this order: persisted super properties, call properties, SDK-owned properties, session ID, and groups.
- Define the reserved SDK-owned key set in one private policy helper.
- Route all current and future producers through the same capture path.
- Make enqueue failure observable through logging and an internal result usable by tests.

## Acceptance criteria

- Invalid names create no event, persistence write, or request.
- Call properties override super properties, while SDK/session/group values override both.
- Duplicate caller keys use deterministic last-write behavior inside the caller builder.
- Valid null property input is safe and retains SDK enrichment.
- Tests prove reserved precedence for `$lib`, `$lib_version`, `$process_person_profile`, `$session_id`, and `$groups`.
- Run `Scripts/ci-paths.sh` first if the required `CI` symlinks are missing from the worktree, then run `Scripts/run-windows-tests.sh`; the Unreal Automation tests must pass and their output must be recorded as a Zeroshot validation gate.

## Exclusions

- A later `BeforeSend` interceptor may intentionally alter SDK fields; EP-006 owns that final override.
- Do not implement super-property, session, or group storage here.

## Unity references

- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/PostHogSDK.cs` (`CaptureInternal`, `AddSdkProperties`)
