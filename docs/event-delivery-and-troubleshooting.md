---
layout: default
title: Event Delivery and Troubleshooting
parent: Reliability and Diagnostics
nav_order: 2
---

# Event Delivery and Troubleshooting

UnrealHog persists accepted events and delivers them asynchronously. `CaptureEvent()` means that the event was offered to the local queue; it does not mean that PostHog has received it.

## Delivery lifecycle

1. UnrealHog validates the event and checks consent.
2. It builds the event, applies SDK and custom properties, and queues a JSON record for file-backed persistence.
3. A threshold, timer, background transition, manual `Flush()`, or `FlushAndQuit()` starts a drain.
4. The queue sends up to [**Max Batch Size**](configuration.html#event-delivery) events per request and continues until it drains or encounters a stopping condition.
5. Successfully delivered records are deleted. Retryable failures remain persisted for a later attempt.

The default queue records are under:

```text
<Project Saved Directory>/UnrealHog/Queue/
```

The actual library directory uses the plugin's friendly name from its descriptor, with `UnrealHog` as the fallback. State such as consent and identity is stored in the adjacent `State` directory.

{: .note}
> No queue or state file is created before collection is permitted. Opting out cancels active delivery, clears queued events, and persists the opted-out choice. Other state files can remain on disk but are not loaded into an active collection runtime while opted out.

## What triggers a flush

| Trigger | Behavior |
|:--|:--|
| Queue reaches **Flush Event Count** | Starts an asynchronous flush immediately after the new event is persisted. Default: `20` events. |
| Flush timer | Attempts a flush every **Flush Interval Seconds** while opted in. Default: `30` seconds. |
| Application enters the background | Starts a best-effort flush, then waits for pending file writes so accepted records are durable before suspension. |
| `Flush()` | Requests a complete asynchronous drain and returns its immediate acceptance result. |
| Normal window close with **Flush On Quit** enabled | Waits for a drain or the configured timeout, then requests exit. |
| `FlushAndQuit()` | Explicitly runs the same bounded drain-then-exit sequence, regardless of **Flush On Quit**. |

Avoid calling `Flush()` after every production event. Normal batching reduces request count, radio wakeups, and battery use (where applicable).

## Capacity and batching

[**Max Queue Size**](configuration.html#event-delivery) defaults to 1,000 persisted events, and [**Max Batch Size**](configuration.html#event-delivery) defaults to 50 events per HTTP request. A flush may send several batches.

When the queue is full, UnrealHog removes the oldest persisted event that is not currently being sent, then saves the new event. If every record is in flight or the old record cannot be deleted, the new event is rejected and UnrealHog logs a warning.

## Offline and retry behavior

| Condition | Queue behavior | Flush outcome |
|:--|:--|:--|
| Platform reports no network reachability | No request starts; records remain queued. | `SkippedOffline` |
| Network/start failure, HTTP `5xx`, or another retryable response | Attempted records remain queued. Retry delay increases by 5 seconds per consecutive failure, capped at 30 seconds. | `Failed` |
| A flush is requested during retry delay | No request starts; records remain queued. | `Paused` |
| HTTP `413 Payload Too Large` | Records remain queued; in-memory batch and automatic-flush limits are halved, down to 1, for the rest of that queue lifetime. | `Failed` |
| HTTP `4xx` other than `413` | The attempted batch is treated as permanently invalid and deleted to keep the queue moving. | `Failed` |
| Successful response | Sent records are deleted and the drain continues. | `Drained` when the queue reaches zero |

A successful later batch resets the retry delay. Configuration defaults are not rewritten when a `413` temporarily reduces the in-memory limits.

## Understanding manual flush results

`Flush()` has two kinds of result:

| Type | When available | Meaning |
|:--|:--|:--|
| `EPostHogFlushRequestResult` | Returned immediately in C++ and Blueprint | Whether the request `Started`, joined an `AlreadyInProgress` drain, or was `Skipped`. It is not a delivery receipt. |
| `EPostHogFlushOutcome` | Delivered to the C++ completion delegate | How the shared drain ended: `Drained`, `Empty`, `Failed`, `Cancelled`, `ProgressBlocked`, `Paused`, or `SkippedOffline`. |

```cpp
const EPostHogFlushRequestResult RequestResult = PostHog->Flush(
	FPostHogFlushCompletedDelegate::CreateWeakLambda(
		this,
		[](EPostHogFlushOutcome Outcome)
		{
			UE_LOG(LogTemp, Display, TEXT("PostHog flush outcome: %d"),
				static_cast<int32>(Outcome));
		}));
```

Blueprint receives the immediate request result. Use UnrealHog logs or a C++ delegate when you need the eventual diagnostic outcome.

## Events are not appearing in PostHog

Work through these checks in order:

1. **Use the right project.** Confirm that you are viewing the same PostHog project whose public project token is configured in Unreal.
2. **Check the developer kill switch.** [**Analytics Enabled**](configuration.html#core-settings) must be selected.
3. **Check consent at runtime.** `IsAnalyticsOptedIn()` must return `true`. If opt-in is rejected, inspect the Unreal log for the validation reason.
4. **Check the token.** [**Project Public API Key**](configuration.html#core-settings) cannot be empty. Use a public project token, normally beginning with `phc_`, not a personal API key.
5. **Check the host.** Match `US` or `EU` to the PostHog project. For `Custom`, enter only the base URL; UnrealHog adds `/batch`.
6. **Use a valid event name.** Empty or whitespace-only event and screen names are rejected.
7. **Request one development flush.** `Started` or `AlreadyInProgress` only confirms acceptance; check the eventual outcome and logs.
8. **Check connectivity.** An offline device retains the queue. Firewalls, TLS configuration, proxies, and platform network permissions can block a custom host.
9. **Inspect `LogUnrealHog`.** Set **Log Level** to `Debug` as described in the [Configuration Reference](configuration.html#logging), then restart the current Play In Editor session or game instance. A status `0` usually means the request did not start or no HTTP response was available.
10. **Interpret the HTTP status.** `5xx` and `413` retain the batch for retry. Other `4xx` responses are permanent and remove the attempted batch, so correct the token, host, or payload before sending another test event.
11. **Allow for ingestion delay.** Keep the game running until the flush resolves, then refresh PostHog's recent event view and search for the exact event name.

If events appear in development but not in a packaged game, compare that build's config, consent path, host reachability, and platform network permissions with the editor environment.

## Queue and storage problems

- A warning about queue capacity means old events are being evicted or the new event could not be persisted.
- `ProgressBlocked` means UnrealHog could not load or delete a queue record while draining. Check that the process can read and write the project Saved directory.
- Corrupt queue records are deleted when possible so later events can continue. A deletion failure stops the drain to avoid an infinite loop.
- `Cancelled` can occur when collection is disabled or shutdown cancels an in-flight request.
- Opting out intentionally clears queued analytics. Do not use opt-out as a delivery troubleshooting step if you need those pending events.

{% include page-footer.html title="Error Tracking" url="/error-tracking.html" %}
