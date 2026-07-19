# SDKP-020: Broaden automatic nonfatal exception capture deliberately

## Status and dependencies

- **State:** Blocked
- **Blocked by:** SDKP-019, EP-015
- **Blocks:** SDKP-021
- **Parity row:** Unreal-native logged/unhandled nonfatal exception signals

## Goal

Cover the useful nonfatal exception signals Unreal exposes while making handled semantics, recursion safety, and unsupported fatal paths explicit.

## Required changes

- Inventory supported UE 5.8 nonfatal delegates and logging hooks through targeted engine-source lookups before selecting integrations.
- Keep `FCoreDelegates::OnEnsureFailed` and add only sources that provide a defensible exception message/stack boundary; do not treat every ordinary Error log as an exception.
- Document and implement one handled-state mapping: explicit `CaptureException` uses caller state, recoverable `ensure` remains handled, and automatic unhandled/logged-exception integrations use `handled=false`.
- Route every source through SDKP-019's shared builder and the normal consent/event pipeline.
- Add reentrancy, debounce, thread-hop, registration, editor-policy, shutdown, and UnrealHog-self-log guards.

## Acceptance criteria

- A checked-in policy table names each supported signal, its handled value, available stack source, thread behavior, and reason for inclusion.
- Each supported signal emits at most one `$exception` event with the expected source/mechanism and rich-list representation.
- Normal warnings/errors outside the allowlisted exception boundary are not captured.
- Recursive UnrealHog logging, concurrent signals, debounce, opt-out, editor disablement, and teardown cannot crash or leak delegate registrations.
- Fatal crash capture remains explicitly unsupported and is not simulated in Automation.
- Tests invoke adapters/delegates through seams rather than crashing or globally polluting unrelated tests.
- Run `Scripts/ci-paths.sh` first if required `CI` symlinks are missing, then run `Scripts/run-windows-tests.sh`; record the passing Unreal Automation output as the Zeroshot validation gate.
- When complete, set this task to `Completed` and replace its index status with `✅`.

## Exclusions

- Do not capture all `UE_LOG(Error, ...)` records indiscriminately.
- Do not hook fatal crash handlers, platform signal handlers, or unsafe post-crash network I/O.
- Do not change manual exception validation.

## Unity references

- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/ErrorTracking/ExceptionManager.cs`
- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/ErrorTracking/UnityExceptionIntegration.cs`
- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/ErrorTracking/WebGLExceptionIntegration.cs`
- `Design/Reference/posthog-unity/tests/PostHog.Unity.Tests/UnityExceptionIntegrationTests.cs`
- `Design/Reference/posthog-unity/tests/PostHog.Unity.Tests/WebGLExceptionIntegrationTests.cs`

## Engine verification scope

- Consult only targeted delegate/logging subtrees under `CI/UnrealEngine/Engine/Source/Runtime/Core`; never edit engine source.
