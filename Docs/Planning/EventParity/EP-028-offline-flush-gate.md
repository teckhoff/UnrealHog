# EP-028: Skip flush attempts while known offline

## Status and dependencies

- **State:** Blocked
- **Blocked by:** EP-022, EP-024
- **Blocks:** EP-029
- **Parity row:** Unity network reachability gate

## Goal

Avoid starting HTTP delivery when Unreal can authoritatively report that the platform has no network connectivity, while retaining every queued event for a later attempt.

## Required changes

- Add a private injectable reachability provider with `Unknown`, `Reachable`, and `NotReachable` states.
- Consult it at the start of a flush before selecting or marking a batch in flight.
- Treat `NotReachable` as a skipped attempt: send no request, delete nothing, and preserve retry state.
- Treat `Unknown` as permission to try HTTP so unsupported platforms do not disable ingestion.
- Trigger or permit a normal timer/manual retry after connectivity can return.

## Acceptance criteria

- Known-offline flushes create no transport request and leave queue contents unchanged.
- Reachable and unknown states proceed through normal batching and error policy.
- A transition from offline to reachable allows the next flush to drain without re-enqueueing.
- The reachability adapter has no public SDK surface and tests use a fake provider.
- Windows verifies any platform delegate/module integration; WSL performs static checks only.

## Exclusions

- Do not implement a custom network monitor or active probe.
- Do not clear or reset EP-024 backoff solely because reachability changes.

## Unity references

- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/Core/EventQueue.cs` (`Application.internetReachability` check)

