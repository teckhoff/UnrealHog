---
layout: default
title: Configuration Reference
parent: Getting Started
nav_order: 3
---

# Configuration Reference

UnrealHog settings are under **Edit > Project Settings > Analytics > PostHog**. They are project-wide `UDeveloperSettings` values stored in the Analytics config.

The runtime reads these settings when the game instance subsystem initializes and when collection is enabled. After changing a setting, restart the current Play In Editor session or game instance before testing it.

## Core settings

| Setting | Default | Effect |
|:--|:--|:--|
| **Analytics Enabled** | `true` | Developer kill switch. When `false`, an end-user opt-in cannot start collection. |
| **Project Public API Key** | Empty | Public project token included with ingestion batches. An empty or whitespace-only value prevents opt-in. Use the project token, usually beginning with `phc_`, not a personal API key. |
| **Default User Opt-In** | `false` | Starting consent state only when UnrealHog cannot load a saved choice. An explicit call to `SetAnalyticsOptIn` is persisted and takes precedence on later launches. |

{: .warning-title}
> Consent is a separate control
>
> `Analytics Enabled` being `true` does not opt a player in. With the default settings, UnrealHog creates no event payload, queue record, persistence file, or HTTP request until collection is permitted.

## Host

| Setting | Default | Valid values and effect |
|:--|:--|:--|
| **Host Type** | `US` | `US` resolves to `https://us.i.posthog.com`; `EU` resolves to `https://eu.i.posthog.com`; `Custom` uses **Host**. |
| **Host** | `https://us.i.posthog.com` | Used only for `Custom`. Leading/trailing whitespace and trailing slashes are removed, then UnrealHog posts to `<host>/batch`. If the normalized host is empty, UnrealHog falls back to the US host. |

Use the base ingestion URL. Do not include `/batch` yourself. For a custom deployment, use an absolute `https://` URL unless you deliberately control a trusted non-TLS development environment.

## Event Delivery

| Setting | Default | Range | Effect |
|:--|:--|:--|:--|
| **Flush Event Count** | `20` | At least `1` | Reaching this many persisted events triggers an asynchronous flush. |
| **Flush Interval Seconds** | `30` | At least `1` | How often the active game instance attempts an automatic flush while opted in. |
| **Max Queue Size** | `1000` | At least `1` | Maximum persisted events. At capacity, UnrealHog removes the oldest event that is not currently in flight before saving a new one. |
| **Max Batch Size** | `50` | At least `1` | Maximum events in one HTTP batch. A complete flush can send multiple batches. |

See [Event Delivery and Troubleshooting](event-delivery-and-troubleshooting.html) for retry, offline, queue, and flush behavior.

## Lifecycle

| Setting | Default | Effect |
|:--|:--|:--|
| **Capture Application Lifecycle Events** | `true` | Captures consent-gated install, update, open, and background lifecycle events. |

## User Profiles

| Setting | Default | Values |
|:--|:--|:--|
| **Person Profiles** | `Identified Only` | `Always` creates or updates profiles for anonymous and identified users; `Identified Only` does so after identification; `Never` disables person-profile updates. |

Choose the narrowest policy that supports your analysis. This setting affects PostHog person processing; it does not replace consent.

## Identity

| Setting | Default | Effect |
|:--|:--|:--|
| **Reuse Anonymous ID** | `false` | When `false`, `Reset()` creates a new anonymous ID. When `true`, it restores the existing anonymous ID after clearing the identified user and groups. |

## Shutdown

| Setting | Default | Range | Effect |
|:--|:--|:--|:--|
| **Flush On Quit** | `true` | Boolean | Gives a normal window-close request a bounded opportunity to drain queued events before UnrealHog requests exit. |
| **Flush On Quit Timeout Seconds** | `3.0` | `0.0` or greater | Maximum wait for that bounded drain. A timeout does not guarantee delivery; durable records remain for a later consented run. |

An explicit `FlushAndQuit()` uses the same bounded drain even when **Flush On Quit** is disabled.

## Exception Tracking

| Setting | Default | Range | Effect |
|:--|:--|:--|:--|
| **Capture Exceptions** | `true` | Boolean | Registers automatic `ensure` capture while the player is opted in. |
| **Exception Debounce Interval Ms** | `1000` | `0` or greater | Minimum interval between automatically captured exceptions. Set to `0` to disable debouncing. |
| **Capture Exceptions In Editor** | `true` | Boolean | Allows automatic exception capture during editor sessions. |

Automatic exception tracking is not crash reporting. See [Error Tracking](error-tracking.html).

## Logging

**Log Level** sets the minimum severity for UnrealHog's own `LogUnrealHog` diagnostics when the game instance subsystem initializes. Its serialized default is `Warning`.

| Value | Output |
|:--|:--|
| `Debug` | Debug, info, warning, and error messages |
| `Info` | Info, warning, and error messages |
| `Warning` | Warning and error messages |
| `Error` | Error messages only |
| `None` | No UnrealHog diagnostic messages |

This setting controls SDK operational diagnostics, not which game logs are captured by session replay.

For a temporary live override, set the Unreal category directly:

```ini
[Core.Log]
LogUnrealHog=VeryVerbose
```

You can also run `Log LogUnrealHog VeryVerbose` as a console command to modify the logging level for the currently running process. The override lasts until another console change or a game instance subsystem initializes and reapplies **Log Level**. Restore normal verbosity after troubleshooting because debug logs can be noisy.

## Unavailable compatibility settings

The editor serializes these settings for future compatibility, but they have no runtime effect:

| Area | Setting | Serialized default | Availability |
|:--|:--|:--|:--|
| Feature Flags | **Preload Feature Flags** | `true` | Unavailable until SDKP-012 |
| Feature Flags | **Feature Flag Request Max Retries** | `1` | Unavailable until SDKP-012 |
| Feature Flags | **Send Feature Flag Event** | `true` | Unavailable until SDKP-012 |
| Feature Flags | **Send Default Person Properties For Flags** | `true` | Unavailable until SDKP-012 |
| Session Replay | **Session Replay** | `false` | Unavailable until SDKP-018 |
| Session Replay | **Throttle Delay Seconds** | `1.0` | Unavailable until SDKP-018 |
| Session Replay | **Screenshot Quality** | `80` | Unavailable until SDKP-018 |
| Session Replay | **Capture Network Telemetry** | `true` | Unavailable until SDKP-018 |
| Session Replay | **Capture Logs** | `false` | Unavailable until SDKP-018 |
| Session Replay | **Minimum Log Level** | `Error` | Unavailable until SDKP-018 |
| Session Replay | **Screenshot Scale** | `0.75` | Unavailable until SDKP-018 |
| Session Replay | **Flush Event Count** | `20` | Unavailable until SDKP-018 |
| Session Replay | **Flush Interval Seconds** | `30` | Unavailable until SDKP-018 |
| Session Replay | **Max Queue Size** | `100` | Unavailable until SDKP-018 |

Do not enable or plan production behavior around these placeholders. See the [feature support table](index.html#feature-support) for currently implemented capabilities.

## Config file

For source control or automated project setup, the settings use `Config/DefaultAnalytics.ini`:

```ini
[/Script/UnrealHog.PostHogDeveloperSettings]
bAnalyticsEnabled=True
ApiKey=phc_replace_with_your_development_project_token
bDefaultUserOptIn=False
HostType=US
FlushEventCount=20
FlushIntervalSeconds=30
MaxQueueSize=1000
MaxBatchSize=50
```

Prefer the Project Settings UI when first configuring the plugin so Unreal writes the correct enum and property serialization for your engine build.

{% include page-footer.html title="Accessing the Subsystem" url="/accessing-subsystem.html" %}
