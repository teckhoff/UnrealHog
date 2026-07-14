# EP-019: Make storage authoritative for the event queue

## Status and dependencies

- **State:** Blocked
- **Blocked by:** EP-001, EP-018
- **Blocks:** EP-020–EP-022, EP-029
- **Parity row:** Durable restart recovery and queue count

## Goal

Eliminate the process-only event array so every persisted event—including records from prior runs—is eligible for threshold checks and delivery.

## Required changes

- Treat `IPostHogStorageProvider::GetEventIds` and `GetEventCount` as the queue source of truth.
- Enqueue by persisting once, then evaluate thresholds from storage count.
- Select flush candidates by deterministic storage ID order and load them through EP-018.
- Track only active request IDs in memory; never require a duplicate in-memory event copy.
- Preserve existing queued files across initialization and immediately make them visible to manual/timer flushes.

## Acceptance criteria

- A queue built over pre-seeded storage reports and sends those events without a new capture.
- A simulated restart retains order, UUIDs, timestamps, and properties.
- Enqueue, count, and threshold behavior reflect asynchronous writes through the storage index.
- Cancellation leaves unsent persisted records eligible for the next queue instance.
- Tests use fake storage/transport and include current-run plus prior-run records.

## Exclusions

- Capacity eviction is EP-020; corrupt-record deletion is EP-021.
- Do not change retry classification or drain more than the existing batch behavior yet.

## Unity references

- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/Core/EventQueue.cs` (`Enqueue`, `Count`, `LoadEvents`)
