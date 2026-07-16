# EP-006: Add a before-send event interceptor

## Status and dependencies

- **State:** Completed
- **Blocked by:** EP-004
- **Blocks:** EP-029
- **Parity row:** Unity `BeforeSend`

## Goal

Allow C++ consumers to inspect, modify, or drop a fully composed event immediately before it is persisted.

## Required changes

- Add one C++ delegate/configuration hook operating on an SDK event view or mutable property representation.
- Invoke it after all enrichment and before queue persistence.
- Represent an intentional drop distinctly from an interceptor failure; both must prevent persistence.
- Catch or safely contain failures according to Unreal delegate conventions and log once.

## Acceptance criteria

- The hook sees event name, distinct ID, UUID, timestamp, and final properties.
- It can remove/add properties and the persisted event reflects the result.
- A drop or failure creates no queue record or HTTP request.
- No hook leaves capture behavior unchanged and allocation overhead bounded.
- Windows Automation covers modify, drop, and failure paths.
- Run `Scripts/ci-paths.sh` first if the required `CI` symlinks are missing from the worktree, then run `Scripts/run-windows-tests.sh`; the Unreal Automation tests must pass and their output must be recorded as a Zeroshot validation gate.

## Exclusions

- Do not make the hook Blueprint-callable in this task; arbitrary synchronous Blueprint mutation during capture requires a separate API decision.
- Do not run the hook again on persisted-event retry.

## Unity references

- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/PostHogConfig.cs` (`BeforeSend`)
- `Docs/Reference/posthog-unity/tests/PostHog.Unity.Tests/PostHogSDKTests.cs` (`TheBeforeSendCallback`)
