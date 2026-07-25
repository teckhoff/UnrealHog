---
layout: default
title: Getting Started
nav_order: 2
has_children: true
---

# Getting Started

Use this path to install UnrealHog and confirm that one consented event reaches a development PostHog project:

1. [Install UnrealHog](installation.html) and verify that Unreal Engine loads the plugin.
2. Follow the [Quickstart](quickstart.html) to configure a project token, grant consent in a development flow, capture an event, and verify delivery.
3. Review the [Configuration Reference](configuration.html) before choosing production defaults.
4. Learn how to [access the runtime subsystem](accessing-subsystem.html) from C++ or Blueprint.
5. Connect your game's consent UI to the [user opt-in API](user-opt-in-status.html).

{: .warning-title}
> Use a development PostHog project
>
> Keep onboarding events separate from production analytics. The quickstart opts in explicitly for verification; do not ship that forced opt-in or the test event.

{% include page-footer.html title="Installation" url="/installation.html" %}
