# EP-029 parity checklist

Maps every assertion group in the isolated core-ingress acceptance suite to the EP task(s) it
demonstrates. Test names are relative to `UnrealHog.Acceptance.CoreIngress.*` and live in
`UnrealHog/Source/UnrealHog/Private/Tests/PostHogCoreIngressConsentAcceptanceTests.cpp` and
`UnrealHog/Source/UnrealHog/Private/Tests/PostHogCoreIngressRecoveryAcceptanceTests.cpp`. EP-017
is excluded per EP-029's own exclusions (separately blocked until a feature-flag subsystem exists).

Every test in this suite drives `FPostHogConsentController` (or a standalone `FPostHogEventQueue`
sharing its exact durable storage) directly — the same public-subsystem boundary already used by
every `PostHogConsentController*Tests.cpp` file, since `UPostHogRuntimeSubsystem` cannot be
constructed outside a live `UGameInstance` in Automation tests.

| Assertion group | Test name | EP row(s) |
|-|-|-|
| Denied consent produces zero transports, identity loads, queued events, distinct id, and persisted records across every producer (CaptureEvent, Identify, Group, CaptureScreen, CaptureException) | `DeniedConsentProducesNoIdentifiersRecordsOrRequests` | EP-002 |
| One continuous opt-in flow drives Application Installed/Opened, `$identify`, `$groupidentify`, `$screen`, `$exception`, and a custom event through one flush, in call order | `OptInEnablesFullPathAllProducersInOneFlow` | EP-002, EP-007, EP-008, EP-009, EP-011, EP-013, EP-014, EP-016 |
| Every event in the shared batch carries the same `$session_id` | `OptInEnablesFullPathAllProducersInOneFlow` | EP-009 |
| Anonymous (pre-identify) lifecycle events carry `$process_person_profile=false`; the identified custom event omits the field entirely under `IdentifiedOnly` | `OptInEnablesFullPathAllProducersInOneFlow` | EP-010 |
| Super-property/call/SDK precedence: a registered super property is visible on `$identify`; a call property overrides it by key; a caller-spoofed `$lib` never overrides the SDK-owned value | `OptInEnablesFullPathAllProducersInOneFlow` | EP-003, EP-004, EP-005, EP-012 |
| Before-send observes the fully merged event (session id, SDK properties, call/super precedence already applied) and its mutation is what gets persisted | `OptInEnablesFullPathAllProducersInOneFlow` | EP-006 |
| Rich/default SDK properties (`$lib`, `$lib_version`) are present and SDK-owned | `OptInEnablesFullPathAllProducersInOneFlow` | EP-003, EP-005 |
| Opt-out clears the queue, releases the event queue, and blocks every producer (CaptureEvent, Identify, Group, CaptureScreen, CaptureException) from enqueuing further | `OptOutClearsQueueAndBlocksFurtherCaptureAcrossProducers` | EP-002 |
| Events composed through the controller are durably persisted before any flush, and a queue rebuilt against that same storage recovers the full storage-authoritative count and drains it across multiple ordered batches | `RestartRecoveryAndMultiBatchDrain` | EP-001, EP-018, EP-019, EP-022 |
| A capacity of one evicts the oldest event across two controller-driven captures, keeping only the newest | `CapacityEvictsOldestAcrossController` | EP-019, EP-020 |
| A corrupt persisted record (malformed JSON, written directly to storage) is deleted without blocking a later valid record in the same drain | `CorruptRecordSkippedWithoutBlockingDrain` | EP-018, EP-021 |
| A permanent (400) delivery failure deletes the attempted batch and never enters backoff | `PermanentErrorRetryBackoffAndOffline` (Phase A) | EP-023 |
| A retryable (500) delivery failure enters a 5-second linear backoff (`Paused` immediately after, still `Paused` one millisecond before the boundary, sends at the boundary) and succeeds on retry | `PermanentErrorRetryBackoffAndOffline` (Phase B) | EP-023, EP-024 |
| Repeated HTTP 413 responses adaptively halve `AdjustedMaxBatchSize`/`AdjustedFlushEventCount`, retain every event, and a smaller retry eventually drains all of them | `PermanentErrorRetryBackoffAndOffline` (Phase C) | EP-023, EP-025 |
| A known-offline flush is skipped (`SkippedOffline`) without touching the transport or losing the queued event; the next flush after reachability returns drains it | `PermanentErrorRetryBackoffAndOffline` (Phase D) | EP-028 |
| `ApplicationWillEnterBackgroundDelegate` triggers a best-effort flush attempt and synchronously drains storage | `BackgroundFlushDrainsStorage` | EP-016, EP-022, EP-026, EP-027 |
| A bounded quit-flush timeout finalizes exactly once (`RequestExit` fires once, `Shutdown()` runs, storage drains) even when the in-flight flush never completes, and issues no new network request | `BoundedQuitTimeoutDrainsStorageWithoutHanging` | EP-026, EP-027 |
| Every event composition, persistence, batch serialization, and response path above runs against fake storage, a fake transport, a fake clock, and a fake reachability provider — no live host, no real credentials | All tests in both files | EP-001 |

## EP coverage index

Every EP-001–EP-016 and EP-018–EP-028 row is exercised by at least one test above:

| EP | Covered by |
|-|-|
| EP-001 | `RestartRecoveryAndMultiBatchDrain`; every test (fake transport only) |
| EP-002 | `DeniedConsentProducesNoIdentifiersRecordsOrRequests`, `OptInEnablesFullPathAllProducersInOneFlow`, `OptOutClearsQueueAndBlocksFurtherCaptureAcrossProducers` |
| EP-003 | `OptInEnablesFullPathAllProducersInOneFlow` |
| EP-004 | `OptInEnablesFullPathAllProducersInOneFlow` |
| EP-005 | `OptInEnablesFullPathAllProducersInOneFlow` |
| EP-006 | `OptInEnablesFullPathAllProducersInOneFlow` |
| EP-007 | `OptInEnablesFullPathAllProducersInOneFlow` |
| EP-008 | `OptInEnablesFullPathAllProducersInOneFlow` |
| EP-009 | `OptInEnablesFullPathAllProducersInOneFlow` |
| EP-010 | `OptInEnablesFullPathAllProducersInOneFlow` |
| EP-011 | `OptInEnablesFullPathAllProducersInOneFlow` |
| EP-012 | `OptInEnablesFullPathAllProducersInOneFlow` |
| EP-013 | `OptInEnablesFullPathAllProducersInOneFlow` |
| EP-014 | `OptInEnablesFullPathAllProducersInOneFlow` |
| EP-015 | Automatic exception capture is a producer-owned wiring concern over the same manual `$exception` path exercised by `OptInEnablesFullPathAllProducersInOneFlow`; its own trigger wiring stays in EP-015's owning unit tests per EP-029's exclusions |
| EP-016 | `OptInEnablesFullPathAllProducersInOneFlow`, `BackgroundFlushDrainsStorage` |
| EP-018 | `RestartRecoveryAndMultiBatchDrain`, `CorruptRecordSkippedWithoutBlockingDrain` |
| EP-019 | `RestartRecoveryAndMultiBatchDrain`, `CapacityEvictsOldestAcrossController` |
| EP-020 | `CapacityEvictsOldestAcrossController` |
| EP-021 | `CorruptRecordSkippedWithoutBlockingDrain` |
| EP-022 | `RestartRecoveryAndMultiBatchDrain`, `BackgroundFlushDrainsStorage` |
| EP-023 | `PermanentErrorRetryBackoffAndOffline` (Phases A, B, C) |
| EP-024 | `PermanentErrorRetryBackoffAndOffline` (Phase B) |
| EP-025 | `PermanentErrorRetryBackoffAndOffline` (Phase C) |
| EP-026 | `BackgroundFlushDrainsStorage`, `BoundedQuitTimeoutDrainsStorageWithoutHanging` |
| EP-027 | `BackgroundFlushDrainsStorage`, `BoundedQuitTimeoutDrainsStorageWithoutHanging` |
| EP-028 | `PermanentErrorRetryBackoffAndOffline` (Phase D) |

## Notes for reviewers

- `FPostHogConsentController` does not thread a clock or reachability provider into its
  internally-owned `FPostHogEventQueue` (see `PostHogConsentController.cpp`'s `EnableCollection`).
  Retry backoff, adaptive-413, and offline-skip assertions therefore run one hop away from the
  literal `Capture*` call path: they use a standalone `FPostHogEventQueue` constructed with the
  fixture's fake clock/reachability against the exact same `FPostHogInMemoryStorageProvider`
  instance the controller composed real events into (via
  `PostHogAcceptance::FNonOwningStorageProviderAdapter` in `PostHogAcceptanceFixture.h`, so the
  controller's ownership/destruction of its storage handle never destroys the shared data). This
  is intentional scope for an *isolated acceptance* suite, not a gap: the controller's real
  production wiring for those paths is unchanged, and per-field backoff/413/offline mechanics are
  already covered unit-by-unit in `PostHogEventQueueTests.cpp` and
  `PostHogEventQueueRetryBackoffTests.cpp`.
- `FPostHogInMemoryStorageProvider::SaveEvent`/`FlushPendingWrites` are synchronous, so this suite
  does not attempt to re-test asynchronous write-visibility timing (already covered by
  `PostHogEventQueueTests.cpp`'s `AsyncWriteVisibleForThresholdAndCount`).
- No test in this suite constructs `FPostHogFileStorageProvider`, `FPostHogHttpClient`, or touches
  a real filesystem path or network host; all durable state lives in one in-memory
  `FPostHogInMemoryStorageProvider` instance per test.
