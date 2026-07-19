# SDKP-019: Add bounded rich exception lists

## Status and dependencies

- **State:** Ready
- **Blocked by:** EP-014 (completed)
- **Blocks:** SDKP-020, SDKP-021
- **Parity row:** Exception causes, aggregate entries, and structured stack frames

## Goal

Represent more than one related exception and produce useful bounded C++ stack-frame data without mechanically translating managed exception objects.

## Required changes

- Add idiomatic public C++/Blueprint input shapes for an exception entry, optional causes, and structured frames while preserving the existing single raw-stack input path.
- Build `$exception_list` in deterministic outer-to-inner order and guard against cycles or repeated entries in C++-constructed graphs.
- Parse supported Unreal stack lines into stable frame fields such as function, module/file, line, platform, and language; retain a raw fallback when parsing fails.
- Cap traversal at depth 4, 50 exception entries, and 50 frames per entry.
- Keep top-level `$exception_type` and `$exception_message` aligned with the primary entry and retain SDK-owned precedence.

## Acceptance criteria

- Single, chained, branched/aggregate, cyclic, over-depth, over-count, and over-frame fixtures serialize deterministically without recursion or unbounded allocation.
- Existing `FPostHogExceptionInput` callers keep their observable single-entry behavior unless they opt into richer data.
- Windows, macOS, Linux, Android, iOS, and unknown/raw sample lines fail soft and never invent unavailable source data.
- Manual handled state propagates consistently to every entry's mechanism.
- Unicode messages, types, functions, and file paths survive JSON serialization.
- Tests use captured text fixtures and constructed inputs; no deliberate crash or platform symbol server is required.
- Run `Scripts/ci-paths.sh` first if required `CI` symlinks are missing, then run `Scripts/run-windows-tests.sh`; record the passing Unreal Automation output as the Zeroshot validation gate.
- When complete, set this task to `Completed` and replace its index status with `✅`.

## Exclusions

- Do not copy Unity/Sentry parser code or promise symbolication.
- Do not hook new engine delegates; SDKP-020 owns automatic signals.
- Do not require managed-language exception semantics from native C++.

## Unity references

- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/ErrorTracking/ExceptionPropertiesBuilder.cs`
- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/ErrorTracking/UnityStackTraceParser.cs`
- `Design/Reference/posthog-unity/tests/PostHog.Unity.Tests/UnityStackTraceParserTests.cs`
