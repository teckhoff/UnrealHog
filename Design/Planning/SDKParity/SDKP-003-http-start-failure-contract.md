# SDKP-003: Make the production HTTP start-failure contract explicit

## Status and dependencies

- **State:** Ready
- **Blocked by:** None
- **Blocks:** Production-boundary `/batch` confidence
- **Parity row:** EP-001 synchronous send-start completion in the real Unreal adapter

## Goal

Test and enforce exactly-once completion when Unreal's production HTTP request refuses to start, without relying implicitly on UE 5.8 callback behavior.

## Required changes

- Introduce a private injectable HTTP-request factory or equivalent narrow seam around request creation and `ProcessRequest()`.
- Observe the Boolean start result and define one owner for completing `(false, 0, "")` when it is `false`.
- Guard completion so a synchronous failure plus a later engine completion delegate cannot notify the queue twice.
- Return a null or inert request handle consistently with EP-001 after start failure.
- Add adapter tests for URL, headers, body, timeout, cancellation, successful completion, and forced start failure.

## Acceptance criteria

- A forced `ProcessRequest() == false` completes exactly once with failure status `0` and leaves no queue flush in flight.
- A platform callback delivered after that forced failure is ignored safely.
- Successful requests still use `POST <canonical-host>/batch`, JSON headers, SDK User-Agent, and a 10-second timeout.
- Tests use the injected request seam and never open a socket or contact PostHog.
- The implementation does not assume that every future Unreal HTTP backend preserves UE 5.8's current failure-callback behavior.
- Run `Scripts/ci-paths.sh` first if required `CI` symlinks are missing, then run `Scripts/run-windows-tests.sh`; record the passing Unreal Automation output as the Zeroshot validation gate.
- When complete, set this task to `Completed` and replace its index status with `✅`.

## Exclusions

- Do not change queue response classification, backoff, batching, or cancellation policy.
- Do not add a live-server smoke test or expose Unreal HTTP types publicly.

## Unity references

- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/Core/NetworkClient.cs`
- `Design/Reference/posthog-unity/tests/PostHog.Unity.Tests/NetworkClientTests.cs`

## Engine verification reference

- `CI/UnrealEngine/Engine/Source/Runtime/Online/HTTP/Private/HttpRequestCommon.cpp` (targeted UE 5.8 lookup only; never modify)
