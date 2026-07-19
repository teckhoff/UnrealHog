# SDKP-022: Validate storage on restricted target platforms

## Status and dependencies

- **State:** Conditional
- **Blocked by:** A declared supported target whose writable filesystem restrictions are not covered by existing evidence
- **Blocks:** Claiming SDK parity/support on that target
- **Parity row:** Persistent identity, state, event queue, and feature-flag cache storage across supported platforms

## Goal

Use platform evidence to decide whether `FPostHogFileStorageProvider` is sufficient, and add a private alternative only where a supported target requires one.

## Required changes

- List the plugin's claimed target platforms and the writable-location/runtime API constraints relevant to each.
- Run a platform-appropriate provider contract covering state, ordered event records, async visibility, deletion, corruption recovery, and restart persistence.
- If a claimed target cannot satisfy the file provider contract, implement a private platform-selected provider behind `IPostHogStorageProvider` using an approved Unreal persistence API.
- Preserve consent gating: no directory, file, save slot, or state read/write before collection is permitted.
- Record unsupported targets explicitly rather than silently selecting a provider known not to work.

## Acceptance criteria

- The task produces a checked-in support matrix with evidence for every claimed target and no machine-specific paths.
- Each supported provider passes the same behavioral contract for identity, lifecycle/super-property state, queued event ordering, and future flag cache state.
- Simulated or real restart retains valid state; malformed data fails soft; opt-out clears consent-scoped event/flag artifacts.
- Provider selection is private, deterministic, and does not change public subsystem APIs.
- If no claimed target needs an alternative, the task closes with validation evidence and no speculative provider implementation.
- Run all platform-neutral checks plus `Scripts/run-windows-tests.sh` from WSL for every code change; record unavailable target hardware/toolchains as an explicit validation limitation.
- When complete, set this task to `Completed` and replace its index status with `✅` if it became required; otherwise record the validation decision in the index.

## Exclusions

- Do not add a fallback solely because Unity uses PlayerPrefs on WebGL/Switch.
- Do not claim console support without the required SDK/toolchain and target evidence.
- Do not modify Unity reference material or engine source.

## Unity references

- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/Storage/IStorageProvider.cs`
- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/Storage/FileStorageProvider.cs`
- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/Storage/PlayerPrefsStorageProvider.cs`
- `Design/Reference/posthog-unity/tests/PostHog.Unity.Tests/FileStorageProviderTests.cs`
