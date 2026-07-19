# EP-010: Apply the configured person-profile policy

## Status and dependencies

- **State:** Comppleted
- **Blocked by:** EP-004, EP-007
- **Blocks:** EP-029
- **Parity row:** `PersonProfiles` and `$process_person_profile`

## Goal

Replace the hard-coded anonymous-only behavior with policy derived from `EPostHogPersonProfiles` and current identity state.

## Required changes

- Centralize profile policy beside SDK-owned event enrichment.
- Set `$process_person_profile` to `false` for `Never` and for anonymous users under `IdentifiedOnly`.
- Allow profile processing for `Always` and identified users under `IdentifiedOnly` by omitting the false override.
- Ensure caller and super properties cannot override the computed value before EP-006.

## Acceptance criteria

- Automation covers every policy/identity combination.
- Identify changes subsequent event behavior without mutating already queued events.
- Default `IdentifiedOnly` anonymous capture remains profileless.
- The setting is read through a validated runtime snapshot rather than the mutable CDO during capture.
- Run `Scripts/ci-paths.sh` first if the required `CI` symlinks are missing from the worktree, then run `Scripts/run-windows-tests.sh`; the Unreal Automation tests must pass and their output must be recorded as a Zeroshot validation gate.

## Exclusions

- Do not emit identify events or change identity state.
- Preserve the repository policy even if the current Unity `Never` branch is incomplete; `Never` must never process a profile.

## Unity references

- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/PostHogSDK.cs` (`AddSdkProperties`)
- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/PostHogConfig.cs` (`PersonProfiles`)
