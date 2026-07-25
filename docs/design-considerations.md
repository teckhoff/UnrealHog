---
layout: default
title: Design Considerations
parent: Reference
nav_order: 2
---

# Design Considerations

UnrealHog's entire purpose is to:

1. Provide Feature-Parity to the PostHog-Unity SDK;
2. Adapt the SDK to be idiomatic to Unreal Engine;
3. Make it Unreal, meaning create feature sets that only make sense in Unreal Engine

## Consent First

Analytics collection is opt-in by default. Until collection is permitted, the SDK must not create event payloads, queue records, persistence files, or HTTP requests. This protects player privacy and keeps automated tests independent of live PostHog credentials.

## Configuration

Project-level configuration lives in `UPostHogDeveloperSettings`. Host selection is limited to `US`, `EU`, and `Custom`, with region defaults resolving to PostHog's ingestion hosts.

## Runtime Ownership

`UPostHogRuntimeSubsystem` is a `UGameInstanceSubsystem`, so gameplay code can access one runtime analytics coordinator per game instance. Event payload construction, identity state, super properties, persistence, delivery, retry policy, lifecycle hooks, and exception capture remain inside focused plugin components.

## Why not use `IAnalyticsProvider`?

`IAnalyticsProvider` is the intended way to provide analytics integration inside of Unreal Engine, but it doesn't cover the entire feature set that PostHog provides, so the existence of the `UPostHogRuntimeSubsystem` would be required either way. Implementation of an `IAnalyticsProvider` wrapper is being considered for future versions of UnrealHog.

## Delivery

The SDK queues accepted events locally, flushes in batches, and retries transient delivery failures without requiring game code to handle transport details. Queue and retry behavior should preserve event ordering within the constraints of durable storage and bounded queue size.

## Test Isolation

Verification should use isolated automation tests, fake storage, and mocked HTTP transport. Normal acceptance tests must not require real PostHog API keys or network access.

{% include page-footer.html title="FAQ" url="/faq.html" %}
