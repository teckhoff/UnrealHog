# SDKP-014: Add the session-replay transport and queue

## Status and dependencies

- **State:** Blocked
- **Blocked by:** SDKP-002, SDKP-013
- **Blocks:** SDKP-017, SDKP-018
- **Parity row:** Bounded replay delivery to `POST /s/`

## Goal

Deliver replay snapshots through a queue separate from `/batch`, with bounded memory, mockable HTTP, compression, and deterministic retry behavior.

## Required changes

- Add a private replay transport seam and cancellable request handle.
- Send arrays of at most 10 `$snapshot` envelopes to `POST <canonical-host>/s/` with JSON headers and a 30-second timeout.
- Gzip request bodies and set `Content-Encoding: gzip`; fall back to uncompressed JSON if compression fails.
- Add a thread-safe in-memory queue with configured threshold, interval, and oldest-first capacity eviction.
- Drain successive batches on success; delete permanent 4xx failures except 413; retain network/3xx/413/5xx failures with deterministic 5–30 second linear backoff.
- Handle 413 with a smaller retry batch so retrying cannot loop forever at the same payload size.

## Acceptance criteria

- Empty or sessionless snapshot input is ignored without UUID or request creation.
- Capacity eviction drops only the oldest complete snapshot envelope.
- Successful flushes preserve order and drain every available batch; only the acknowledged leading batch is removed.
- Offline reachability skips transport and retains all snapshots.
- Cancellation, shutdown, and synchronous send-start failure complete safely without late callbacks.
- Compression and fallback tests inspect exact bytes/headers through fake transport and never contact PostHog.
- Replay data never enters the normal event queue or `/batch` envelope.
- Run `Scripts/ci-paths.sh` first if required `CI` symlinks are missing, then run `Scripts/run-windows-tests.sh`; record the passing Unreal Automation output as the Zeroshot validation gate.
- When complete, set this task to `Completed` and replace its index status with `✅`.

## Exclusions

- Do not persist replay snapshots to disk in this task; the Unity reference queue is memory-backed.
- Do not capture screenshots, input, logs, or network telemetry.
- Do not reuse `/batch` request timeout or endpoint.

## Unity references

- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/SessionReplay/ReplayQueue.cs`
- `Design/Reference/posthog-unity/tests/PostHog.Unity.Tests/ReplayQueueTests.cs`
