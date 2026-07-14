# EP-020: Enforce queue capacity from persisted storage

## Status and dependencies

- **State:** Blocked
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

## Exclusions

- Do not introduce priority queues or configurable drop policies.
- Do not change configuration validation; limits below one already fail initialization.

## Unity references

- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/Core/EventQueue.cs` (`Enqueue`)
