---
layout: default
title: Data Collected and Stored
parent: Reliability and Diagnostics
nav_order: 1
---

# Data Collected and Stored

This page describes the data UnrealHog `{{ site.unrealhog_version }}` adds to events and persists locally. Use it when designing consent disclosures, retention controls, and account-switch behavior.

{: .warning-title}
> Your game controls its analytics data
>
> UnrealHog cannot determine whether a value is appropriate to collect. Do not put secrets, access tokens, passwords, or unnecessary personal data in event names, properties, identifiers, exception messages, or stack traces. Review your final payloads against your privacy policy and applicable requirements.

## Event Payloads

Every accepted event contains an event name, distinct ID, timestamp, event UUID, and a `properties` object. The network batch also contains the PostHog project token and send time.

UnrealHog adds the following SDK-owned properties when the platform can provide them:

| Area | Properties | Notes |
|:--|:--|:--|
| SDK | `$lib`, `$lib_version` | Identify UnrealHog and its version. |
| Application | `$app_name`, `$app_version`, `$app_build` | Name and version come from General Project Settings. The build value is currently populated only on the iOS platform family. Values can be empty when project or platform metadata is unavailable. |
| Platform and OS | `$platform`, `$platform_variant`, `$os`, `$os_version` | Variant and normalized OS name are omitted when unavailable or unrecognized. |
| Device | `$device_model`, `$device_manufacturer`, `$device_type` | Availability varies by platform. Manufacturer is currently populated for Android and Apple mobile platforms. |
| Viewport | `$screen_width`, `$screen_height` | Added only when a game viewport is available. |
| Identity | `distinct_id`, `$process_person_profile` | `distinct_id` is a top-level event field. Person-profile processing depends on the configured policy and identity state. |
| Session and groups | `$session_id`, `$groups` | Added when a session or group membership is active. |

Custom properties are composed in this order:

1. Registered super properties.
2. Properties supplied with the capture call.
3. Properties owned by the event producer, such as `$screen_name` or exception fields.
4. SDK context, session, and group properties.
5. The C++ [Before Send](before-send.html) callback, if applicable.

Later sources win when a key is repeated. UnrealHog rejects SDK-reserved keys in normal event and super-property builders, but a before-send callback receives the final mutable property object.

## Additional Automatic Data

When **Capture Application Lifecycle Events** is enabled, UnrealHog can capture:

- `Application Installed`, followed by `Application Opened`, the first time lifecycle state is initialized after consent.
- `Application Updated` when the recorded version or build changes.
- `Application Backgrounded` and a later `Application Opened` with `from_background`.
- Current and previous application version/build values where available.

When **Capture Exceptions** is enabled, automatic capture currently listens only for Unreal `ensure` failures. Exception events can contain the message, type, handled state, source, file path, line number, function or frame text, and any custom properties supplied by the game. See [Error Tracking](error-tracking.html) for build and privacy limitations.

## Local Storage

The default file-backed provider stores plain JSON below:

```text
<Project Saved Directory>/UnrealHog/
├── Queue/
│   └── <event UUID>.json
└── State/
    ├── opt_in_status.json
    ├── identity.json
    ├── super_properties.json
    └── lifecycle.json
```

The directory name uses the plugin friendly name and falls back to `UnrealHog`. Files are not encrypted by UnrealHog; rely on suitable platform storage protections and avoid collecting secrets.

Queued event records persist until they are delivered, discarded after a permanent delivery error, evicted for capacity, cleared by opt-out, or removed manually while the game is stopped. Consent, identity/groups, super-property, and lifecycle state can persist across launches. Sessions are held in memory rather than in these state files.

**Max Queue Size** defaults to 1,000 records. When the queue is full, the oldest record not currently in flight is removed before the new event is saved. See [Event Delivery and Troubleshooting](event-delivery-and-troubleshooting.html) for batching and failure behavior.

## State Transitions

| Action or state | Queue and network | Identity and groups | Super properties | Consent and lifecycle state |
|:--|:--|:--|:--|:--|
| Before first opt-in | No event payload, queue record, state file, or HTTP request is created. Calls are dropped rather than deferred. | Not initialized or changed. | Not registered or changed. | No file is created. |
| Opt in | Collection initializes; consented lifecycle events can be queued immediately. | Saved state is loaded, or a new anonymous ID is created. | Saved values are loaded. | The opt-in choice and lifecycle state are persisted. |
| Opt out | Active delivery is cancelled and queued events are deleted. | In-memory state is released, but the current implementation leaves its state file on disk for a later opt-in. | In-memory state is released, but the state file remains for a later opt-in. | The opted-out choice is persisted; lifecycle state remains. |
| `Reset()` | Already-queued events are unchanged. A new session starts. | Identified identity and groups are cleared; a new anonymous ID is normally generated and persisted. | Unchanged. | Consent and lifecycle state are unchanged. |
| Restart while opted in | Durable queued records remain eligible for delivery. | Restored. | Restored. | Restored. |

`Reset()` is an analytics identity transition, not a local-data deletion API. It does not clear consent, queued events, super properties, or lifecycle history. Clear account-sensitive super properties explicitly when signing out.

## Deleting Local Data

UnrealHog `{{ site.unrealhog_version }}` does not expose a public delete-all-state method. To remove every local UnrealHog record, stop the game and delete its `<Project Saved Directory>/UnrealHog/` directory using your platform-appropriate data-management flow.

Do not assume uninstall always removes this directory. Deletion behavior is platform controlled, and if the directory sits outside the installation directory, it might remain. Testing may be required for each supported distribution.

If your product offers an in-game data-deletion request, design and test an explicit flow for local analytics data as well as server-side data.

{% include page-footer.html title="Event Delivery and Troubleshooting" url="/event-delivery-and-troubleshooting.html" %}
