# EP-001: Introduce a mockable batch transport seam

## Status and dependencies

- **State:** Ready
- **Blocked by:** None
- **Blocks:** EP-019, EP-022, EP-029
- **Parity row:** Isolated `/batch` HTTP delivery verification

## Goal

Allow queue behavior to be tested without Unreal's live HTTP stack while preserving the existing request URL, headers, body, timeout, and cancellation behavior.

## Required changes

- Add a private batch-transport interface whose send operation accepts a serialized batch and reports success, status code, and response body asynchronously.
- Adapt `FPostHogHttpClient` to implement the interface; keep it private and keep public headers free of HTTP types.
- Change `FPostHogEventQueue` to depend on the interface and retain a cancellable request handle owned by that abstraction.
- Add a deterministic fake transport for Automation tests that can queue responses and inspect requests.

## Acceptance criteria

- Existing production requests still use `POST <normalized-host>/batch`, JSON headers, `<library>/<version>` User-Agent, and a 10-second timeout.
- Queue tests can complete or cancel requests without constructing `FHttpModule` or contacting a network.
- A synchronous send-start failure completes exactly once with status `0`.
- Cancellation prevents a late callback from accessing a destroyed queue.
- Run `Scripts/ci-paths.sh` first if the required `CI` symlinks are missing from the worktree, then run `Scripts/run-windows-tests.sh`; the Unreal Automation tests must pass and their output must be recorded as a Zeroshot validation gate.
- Do NOT modify any testing scripts in response to failed test. Make a note of the failed test to be deferred to a human. You may still modify non-test project files in response to failed tests.

## Exclusions

- Do not change retry, batching, or response classification.
- Do not expose transport types as SDK API.

## Unity references

- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/Core/NetworkClient.cs`
- `Docs/Reference/posthog-unity/tests/PostHog.Unity.Tests/NetworkClientTests.cs`
