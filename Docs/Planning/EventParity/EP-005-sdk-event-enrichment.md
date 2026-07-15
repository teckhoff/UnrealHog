# EP-005: Complete SDK event enrichment

## Status and dependencies

- **State:** Blocked
- **Blocked by:** EP-003
- **Blocks:** EP-029
- **Parity row:** Default SDK, device, screen, and application properties

## Goal

Add the missing Unity-observable default properties using Unreal-native platform information.

## Required changes

- Add `$os`, `$device_type`, `$device_manufacturer`, and `$app_build` to the private enrichment helper.
- Normalize OS and device-type values to stable PostHog categories rather than raw platform enum spelling.
- Retain existing `$lib`, `$lib_version`, `$os_version`, `$device_model`, dimensions, `$app_name`, and `$app_version` behavior.
- Omit values only when Unreal cannot provide a meaningful value; document platform-specific fallbacks in code.

## Acceptance criteria

- Deterministic tests inject platform/app/screen information and assert exact property names and JSON types.
- Mobile, desktop, and web-like platform mappings have focused cases where supported by Unreal targets.
- SDK-owned values are constructed independently of user property input.
- Existing Unreal-specific `$platform` and `$platform_variant` fields may remain as additive metadata.
- Run `Scripts/ci-paths.sh` first if the required `CI` symlinks are missing from the worktree, then run `Scripts/run-windows-tests.sh`; the Unreal Automation tests must pass and their output must be recorded as a Zeroshot validation gate.

## Exclusions

- Do not query hardware identifiers or add fingerprinting data.
- Do not implement session, identity, or person-profile policy.

## Unity references

- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/PostHogSDK.cs` (`AddSdkProperties`)
- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/Utilities/PlatformInfo.cs`
