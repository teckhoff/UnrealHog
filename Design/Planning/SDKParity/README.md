# Full SDK parity micro-task index

## Scope and rules

This task set turns the confirmed follow-up work from the July 2026 Unity SDK parity review into independently reviewable changes. It complements `Design/Planning/EventParity`: that plan remains the authority for `/batch` event ingress, while this plan covers confirmed corrections, feature flags, session replay, broader error tracking, and one conditional platform-storage validation.

Each `SDKP-###` file is one scoped task. A task whose **Blocked by** list is non-empty must not begin until every listed prerequisite is complete. Completion means its acceptance criteria pass, the Unreal Automation output from `Scripts/run-windows-tests.sh` has been recorded as a Zeroshot validation gate, and the change is available to dependent work.

All paths and references are repository-relative. Unity sources are read-only behavioral references; implementations must remain idiomatic Unreal Engine code and must preserve the repository's stronger consent invariant. Tests must use injected storage, clocks, capture sources, and HTTP transports and must never contact PostHog. If required `CI` symlinks are missing in a worktree, run `Scripts/ci-paths.sh` before the Windows test gate.

The following review items are deliberately not tasks in this plan:

- Sample content and the `MakingItUnreal` proposals are product additions, not required Unity behavior.
- Fatal-crash reporting remains excluded because plugin code cannot safely promise capture during process failure.
- Restricted-platform storage is conditional on supporting a target whose writable filesystem cannot satisfy the existing provider contract; SDKP-022 defines the validation gate rather than assuming a replacement is required.
- Observable intentional deviations already recorded by the review—opt-in persistence, `PersonProfiles::Never`, blank-screen rejection, startup lifecycle deduplication, and Unreal-only convenience APIs—remain product decisions unless separately reopened.

## Dependency graph

```text
Core review follow-ups
SDKP-001 offline outcome
SDKP-002 host normalization ──────┬─> SDKP-007 flags transport
                                  └─> SDKP-014 replay transport
SDKP-003 HTTP start hardening (independent)
SDKP-004 capability clarity (temporary until flags/replay ship)
SDKP-005 exception person URL ────────────────────────┐

Feature flags                                         │
SDKP-006 response models ──┬─> SDKP-007 transport ─┐  │
                           └─> SDKP-008 cache ──────┼─> SDKP-009 public reads
                                                    └─> SDKP-010 evaluation properties
SDKP-009 + SDKP-010 + identity/group ingress ─────────> SDKP-011 lifecycle integration
SDKP-011 ─> EP-017 flag-called ingress ───────────────> SDKP-012 acceptance

Session replay
SDKP-013 models/config ──┬─> SDKP-014 transport/queue ─┐
                        ├─> SDKP-015 screenshot/input ─┼─> SDKP-017 runtime lifecycle/API
                        └─> SDKP-016 logs/network ─────┘             └─> SDKP-018 acceptance

Error tracking
EP-014 ─> SDKP-019 rich exception lists ─> SDKP-020 automatic signal policy
SDKP-005 + SDKP-019 + SDKP-020 ──────────> SDKP-021 acceptance

Platform validation
SDKP-022 is conditional and independent.
```

## Tasks

✅ means the task is completed.
⏳ means the task is currently able to be completed.
❌ means the task is currently blocked from completion by another task.
💀 means the task has been cancelled.
⚙️ means the task only affects certain conditions that may not completely block completion.

| Task | Outcome | Blocked by | Status |
|-|-|-|-|
| [SDKP-001](SDKP-001-public-offline-flush-outcome.md) | Public `SkippedOffline` flush result | None | ✅ |
| [SDKP-002](SDKP-002-normalize-custom-host.md) | One canonical host without trailing slashes | None | ✅ |
| [SDKP-003](SDKP-003-http-start-failure-contract.md) | Production HTTP start completion is explicit and testable | None | ✅ |
| [SDKP-004](SDKP-004-unavailable-capability-settings.md) | Inert settings cannot imply working features | None | ✅ |
| [SDKP-005](SDKP-005-exception-person-url.md) | Exception events include `$exception_personURL` | None | 💀 |
| [SDKP-006](SDKP-006-feature-flag-response-models.md) | Lossless v2 flag response models | None | ✅ |
| [SDKP-007](SDKP-007-feature-flag-transport.md) | Mockable consent-gated `/flags/?v=2` transport | SDKP-002, SDKP-006 | ✅ |
| [SDKP-008](SDKP-008-feature-flag-cache.md) | Consent-safe memory and disk flag cache | SDKP-006, EP-002 | ❌ |
| [SDKP-009](SDKP-009-feature-flag-public-api.md) | Blueprint/C++ reads, reload, payloads, and callback | SDKP-007, SDKP-008 | ❌ |
| [SDKP-010](SDKP-010-feature-flag-evaluation-properties.md) | Persistent person/group evaluation properties | SDKP-007, SDKP-008, SDKP-009 | ❌ |
| [SDKP-011](SDKP-011-feature-flag-identity-lifecycle.md) | Consent, identity, group, reset, and preload integration | SDKP-009, SDKP-010, EP-007, EP-008, EP-011 | ❌ |
| [SDKP-012](SDKP-012-feature-flag-acceptance-suite.md) | Isolated end-to-end feature-flag parity proof | SDKP-006–SDKP-011, EP-017 | ❌ |
| [SDKP-013](SDKP-013-session-replay-models-and-config.md) | Validated replay settings and rrweb-style models | None | ⏳ |
| [SDKP-014](SDKP-014-session-replay-transport-queue.md) | Bounded `/s/` queue with mockable delivery | SDKP-002, SDKP-013 | ❌ |
| [SDKP-015](SDKP-015-session-replay-screenshot-input.md) | Throttled screenshots and pointer input | SDKP-013 | ❌ |
| [SDKP-016](SDKP-016-session-replay-logs-network.md) | Bounded console and network telemetry | SDKP-013 | ❌ |
| [SDKP-017](SDKP-017-session-replay-runtime-lifecycle.md) | Public replay API and consent/session/lifecycle orchestration | SDKP-014–SDKP-016, EP-002, EP-009, EP-013, EP-027 | ❌ |
| [SDKP-018](SDKP-018-session-replay-acceptance-suite.md) | Isolated end-to-end replay parity proof | SDKP-013–SDKP-017 | ❌ |
| [SDKP-019](SDKP-019-rich-exception-list.md) | Bounded exception chains and structured frames | EP-014 (complete) | ⏳ |
| [SDKP-020](SDKP-020-automatic-exception-signal-policy.md) | Deliberate Unreal-native nonfatal signal coverage | SDKP-019, EP-015 | ❌ |
| [SDKP-021](SDKP-021-error-tracking-acceptance-suite.md) | Error-tracking phase-two parity proof | SDKP-005, SDKP-019, SDKP-020 | ❌ |
| [SDKP-022](SDKP-022-restricted-platform-storage-validation.md) | Evidence-backed storage support per target platform | None | ⚙️ |
| [SDKP-023](SDKP-023-configured-log-level.md) | Configured minimum severity for SDK-owned diagnostic logging | None | ✅ |

## Completion boundary

Tasks that are cancelled are no longer considered as a blocker for completion.

The core review follow-ups are complete when SDKP-001 through SDKP-004 pass. Feature-flag parity additionally requires SDKP-006 through SDKP-012 and completion of EP-017. Session-replay parity requires SDKP-013 through SDKP-018. Broader nonfatal error-tracking parity requires SDKP-019 through SDKP-021.

Do not describe total Unity SDK parity as complete while any required family above remains incomplete. SDKP-022 becomes required before claiming support on a restricted target platform.
