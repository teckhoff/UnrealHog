---
layout: default
title: Overview
nav_order: 1
---

# UnrealHog

UnrealHog is an **unofficial** Unreal Engine plugin for sending consent-gated analytics and exception events to PostHog.

{: .warning-title}
> Unofficial Plugin
>
> UnrealHog is not maintained or supported by PostHog.

The SDK keeps analytics collection opt-in by default, exposes C++ and Blueprint APIs, and stores delivery concerns inside the plugin runtime.

**Documentation version:** UnrealHog `{{ site.unrealhog_version }}`

## Get to Your First Event

1. [Install UnrealHog](installation.html).
2. Follow the [Quickstart](quickstart.html) to configure a development project, apply test consent, capture one event, and verify it in PostHog.
3. Review the [Configuration Reference](configuration.html) before choosing production settings.
4. Connect `SetAnalyticsOptIn` to the player's real consent choice.
5. Use [Event Delivery and Troubleshooting](event-delivery-and-troubleshooting.html) if an event does not appear.

Check the [latest UnrealHog release](https://github.com/teckhoff/UnrealHog/releases/latest) for the plugin build that matches your Unreal Engine distribution and platform.

## Supported Products and Features {#feature-support}

| PostHog product or feature | Status | UnrealHog support |
|:---------------------------|:-------|:------------------|
| Product analytics | Supported | Custom events, screen events, event properties, identity, person profiles, super properties, and application lifecycle events |
| Group analytics | Supported | Group membership and group properties; [group analytics is a paid PostHog add-on](https://posthog.com/docs/product-analytics/group-analytics#billing) |
| Error tracking | Supported with limitations | Automatic Unreal `ensure` capture and manually reported handled failures; UnrealHog is not a fatal crash-reporting solution |
| Feature flags | Not yet available | Project settings are serialized for future compatibility but have no runtime effect |
| Session replay | Not yet available | Project settings are serialized for future compatibility but have no runtime effect |

## Documentation

- [Getting Started](getting-started.html): installation, quickstart, configuration, subsystem access, and consent.
- [Analytics](analytics.html): events, identity, super properties, groups, and lifecycle capture.
- [Reliability and Diagnostics](reliability-and-diagnostics.html): collected data, local storage, delivery, troubleshooting, and error tracking.
- [Advanced](advanced.html): before-send filtering, plugin packaging, and design considerations.
- [Reference](reference.html): public APIs and settings.

{% include page-footer.html title="Getting Started" url="/getting-started.html" %}
