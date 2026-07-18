# Event ingress parity micro-task index

## Scope and rules

This task set covers behavioral parity with the Unity SDK for events delivered through PostHog's `/batch` endpoint. It includes capture semantics, consent, identity and sessions, event-producing APIs, persistence, batching, retry, lifecycle delivery, and isolated verification. Feature-flag fetching and session-replay transport are excluded; the `$feature\_flag\_called` producer remains recorded as an externally blocked ingress task.

Each `EP-###` file is one independently reviewable change. A task whose **Blocked by** list is non-empty must not begin until every listed task is complete. Completing a prerequisite means its acceptance criteria pass, its Unreal Automation results from `Scripts/run-windows-tests.sh` have been recorded as a Zeroshot validation gate, and the change is available to the dependent task.

All paths and references are repository-relative. Unity sources are behavioral references only and must remain unchanged. Tests must use fake storage, clocks, and HTTP and must never contact PostHog. Zeroshot workers and validators run in WSL and must perform all available static/platform-neutral checks. They must also run the Windows-side Unreal Automation gate with `Scripts/run-windows-tests.sh` and record its output; if the required `CI` symlinks are missing from the worktree, run `Scripts/ci-paths.sh` first.

## Dependency graph

```text
EP-001 HTTP seam ───────────────┬─> EP-019 storage-backed queue ─> EP-020 capacity
                               │            ├─> EP-021 corrupt records
EP-018 event rehydration ──────┘            └─> EP-022 drain batches
                                                        ├─> EP-023 error policy ─> EP-024 backoff
                                                        │                     └─> EP-025 adaptive 413
                                                        └─> EP-026 public flush ─> EP-027 lifecycle flush

EP-002 consent lifecycle ──┬─> EP-007 identity ─> EP-008 identify/reset/alias
                           │        ├─> EP-010 profile policy
                           │        ├─> EP-011 groups
                           │        └─> EP-017 flag-called event (also externally blocked)
                           ├─> EP-009 sessions ─> EP-016 lifecycle events
                           └─> public producer tasks

EP-003 rich properties ──> EP-004 capture policy ─┬─> EP-006 before-send
                                                  ├─> EP-010 profile policy
                                                  ├─> EP-012 super properties
                                                  ├─> EP-013 screen events
                                                  ├─> EP-014 manual exceptions ─> EP-015 automatic exceptions
                                                  └─> EP-016 lifecycle events

EP-005 SDK enrichment is independent after EP-003.
EP-028 adds the offline reachability gate after retry behavior exists.
EP-029 final acceptance depends on EP-001 through EP-016 and EP-018 through EP-028.
EP-017 is verified separately when the feature-flag subsystem exists.
```

## Tasks
✅ means the task is completed.
⏳ means the task is currently able to be completed.
❌ means the task is currently blocked from completion by another task.

|Task|Outcome|Blocked by|Completed|
|-|-|-|-|
|[EP-001](EP-001-http-transport-seam.md)|Mockable private batch transport|None|✅|
|[EP-002](EP-002-runtime-consent-lifecycle.md)|Runtime opt-in/out without pre-consent side effects|None|✅|
|[EP-003](EP-003-rich-event-property-values.md)|Null, object, and array property values|None|✅|
|[EP-004](EP-004-capture-validation-and-reserved-properties.md)|Capture validation and SDK-owned property precedence|EP-002, EP-003|✅|
|[EP-005](EP-005-sdk-event-enrichment.md)|Missing Unity-equivalent SDK properties|EP-003|✅|
|[EP-006](EP-006-before-send-interceptor.md)|Modify or drop fully enriched events|EP-004|✅|
|[EP-007](EP-007-persistent-identity-manager.md)|Persistent anonymous and identified identity state|EP-002|✅|
|[EP-008](EP-008-identify-reset-and-alias-apis.md)|Identity-producing public event APIs|EP-003, EP-004, EP-007|✅|
|[EP-009](EP-009-session-manager.md)|Independent rotating `$session\_id`|EP-002|✅|
|[EP-010](EP-010-person-profile-policy.md)|Configured `$process\_person\_profile` behavior|EP-004, EP-007|✅|
|[EP-011](EP-011-group-membership-and-events.md)|Persistent groups and `$groupidentify`|EP-003, EP-004, EP-007|✅|
|[EP-012](EP-012-persistent-super-properties.md)|Register/unregister and precedence|EP-003, EP-004|✅|
|[EP-013](EP-013-screen-event-api.md)|`$screen` public producer|EP-003, EP-004|✅|
|[EP-014](EP-014-manual-exception-events.md)|Manual `$exception` capture|EP-003, EP-004, EP-007|✅|
|[EP-015](EP-015-automatic-exception-capture.md)|Configured automatic exception ingress|EP-014|✅|
|[EP-016](EP-016-application-lifecycle-events.md)|Installed/updated/opened/backgrounded events|EP-003, EP-004, EP-009|✅|
|[EP-017](EP-017-feature-flag-called-events.md)|Deduplicated `$feature\_flag\_called` events|EP-003, EP-004, EP-007, external feature-flag subsystem|❌|
|[EP-018](EP-018-persisted-event-rehydration.md)|Lossless stored-event deserialization|None|✅|
|[EP-019](EP-019-storage-authoritative-event-queue.md)|Restart recovery and storage-authoritative count|EP-001, EP-018|✅|
|[EP-020](EP-020-storage-authoritative-queue-capacity.md)|Correct oldest-event eviction across restarts|EP-019|✅|
|[EP-021](EP-021-corrupt-persisted-event-handling.md)|Poison record removal without blocking|EP-018, EP-019|✅|
|[EP-022](EP-022-drain-all-batches-per-flush.md)|One flush drains all available batches|EP-001, EP-019, EP-021|✅|
|[EP-023](EP-023-delivery-error-classification.md)|Retryable versus permanent response policy|EP-022|✅|
|[EP-024](EP-024-transient-retry-backoff.md)|Deterministic 5–30 second retry pause|EP-023|✅|
|[EP-025](EP-025-payload-too-large-adaptation.md)|Adaptive batching after HTTP 413|EP-023|✅|
|[EP-026](EP-026-public-manual-flush.md)|Blueprint/C++ manual flush API|EP-022|✅|
|[EP-027](EP-027-background-and-shutdown-flush.md)|Background persistence and bounded final flush|EP-002, EP-016, EP-022, EP-026|⏳|
|[EP-028](EP-028-offline-flush-gate.md)|Skip HTTP while the platform is known offline|EP-022, EP-024|⏳|
|[EP-029](EP-029-core-ingress-acceptance-suite.md)|End-to-end isolated core ingress acceptance suite|EP-001–EP-016, EP-018–EP-028|❌|

