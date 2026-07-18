# EP-020: Enforce queue capacity from persisted storage

## Status and dependencies

- **State:** Completed
- **Blocked by:** EP-019
- **Blocks:** EP-029
- **Parity row:** `MaxQueueSize` oldest-event eviction

## Goal

Apply the queue limit across current and prior runs without deleting an in-flight record or exceeding the configured persistent count.

## Required changes

- Check persisted count before every save.
- Evict the oldest non-in-flight event by the storage provider's deterministic UUIDv7/lexical ordering.
- If every eligible record is in flight or deletion fails, reject the new event rather than silently exceeding capacity.
- Log the dropped/rejected event condition and return an internal enqueue result.

## Acceptance criteria

- At capacity, one oldest eligible record is deleted before the new record is saved.
- Preexisting records count toward the limit immediately after restart.
- Active batch records are never removed beneath a request.
- Delete/save failures leave the index and disk in a documented consistent state and are observable.
- Tests cover capacity `1`, multiple in-flight IDs, restart state, and failure injection.
- Run `Scripts/ci-paths.sh` first if the required `CI` symlinks are missing from the worktree, then run `Scripts/run-windows-tests.sh`; the Unreal Automation tests must pass and their output must be recorded as a Zeroshot validation gate.

## Exclusions

- Do not introduce priority queues or configurable drop policies.
- Do not change configuration validation; limits below one already fail initialization.

## Unity references

- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/Core/EventQueue.cs` (`Enqueue`)

## Implementation notes

- `FPostHogEventQueue::Enqueue` checks `IPostHogStorageProvider::GetEventCount()` before every save.
- Capacity is storage-authoritative: records persisted by prior runs count immediately after queue construction.
- Eviction scans `GetEventIds()` in deterministic oldest-first UUIDv7/lexical order and deletes the first id that is not in `InFlightEventIds`.
- If storage remains at or above `MaxQueueSize` after a successful delete call, the enqueue is rejected as a capacity delete failure.
- `Enqueue` returns an internal `EPostHogEventQueueEnqueueResult`; public capture APIs continue returning their existing public result types.

## Failure-state notes

- When every persisted id is in flight, the incoming event is rejected, no save is attempted, and storage/index state is unchanged.
- When eviction delete fails, the incoming event is rejected, no save is attempted, and the file storage provider keeps the old id visible in its index because the durable record may still exist.
- When save fails after a successful capacity eviction, the evicted old record remains dropped and the incoming record is not indexed; provider-visible count matches the durable records still present.
- `ClearEvents` now removes ids from the file storage index only after each file delete succeeds; a partial clear returns `false` and leaves failed deletes observable through `GetEventIds()`.

## Rollback note

Revert the touched queue, consent, storage, test, and planning files. No config/schema migration is introduced, and existing UUID-keyed queued JSON files remain readable.
