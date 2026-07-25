---
layout: default
title: Application Lifecycle Events
parent: Analytics
nav_order: 5
---

# Application Lifecycle Events

UnrealHog can automatically capture installation, update, open, and background events. These events help you measure new installations, returning players, version adoption, and application usage without adding calls throughout your game code.

{: .note-title}
> This requires opt-in
>
> No lifecycle event or lifecycle state file is created until the user has opted in. If consent is granted after startup, lifecycle tracking begins when consent is granted. See [User Opt-In Status](user-opt-in-status.html).

## Enabling Lifecycle Events

Open **Edit > Project Settings > Analytics > PostHog** and, under **Lifecycle**, enable `Capture Application Lifecycle Events`. This setting defaults to `true`; no C++ or Blueprint calls are required after the player has opted in.

UnrealHog captures the following events:

| Event | When it is captured | Event properties |
|:------|:--------------------|:-----------------|
| `Application Installed` | The first consented run with no saved lifecycle state. | `version`, `build` |
| `Application Updated` | The saved version or build differs from the current application. | `version`, `build`, `previous_version`, `previous_build` |
| `Application Opened` | Lifecycle tracking starts or the application returns from the background. | `version`, `build`, `from_background` |
| `Application Backgrounded` | The application deactivates, enters the background, or begins terminating. | `version`, `build` |

Version and build values come from the current Unreal application metadata. Lifecycle state is persisted so installation and update events can be distinguished on later launches.

## Flushing On Quit

Open **Edit > Project Settings > Analytics > PostHog** and use the **Shutdown** section to configure the final flush:

- `Flush on Quit` attempts to drain queued events when a window-close request is received and defaults to `true`.
- `Flush on Quit Timeout Seconds` limits how long the final drain may delay exit and defaults to `3.0` seconds.

{: .note}
> Shutdown delivery is best-effort. Offline state, delivery failure, retry backoff, or the configured timeout can prevent queued events from being delivered during the current run.
>
> To give queued events a bounded opportunity to be delivered when the player chooses to quit, call `Flush and Quit` on the PostHog Runtime Subsystem instead of `Quit Game`.

Programmatic quit paths can bypass the window-close handler. When your game provides its own quit button, call `FlushAndQuit` instead of Unreal's regular quit function. It performs a bounded flush and requests engine exit when the queue drains or the timeout is reached.

{% include api-method.md id="flush-and-quit" %}

{% capture cpp_example_flush_and_quit %}
```cpp
#include "Engine/GameInstance.h"
#include "Subsystems/PostHogRuntimeSubsystem.h"

void AExamplePlayerController::QuitGame()
{
	UPostHogRuntimeSubsystem* PostHog = GetGameInstance()->GetSubsystem<UPostHogRuntimeSubsystem>();
	check(PostHog); // Optional

	PostHog->FlushAndQuit();
}
```
{% endcapture %}

{% capture blueprint_example_flush_and_quit_graph %}
{% include blueprints/flushing-on-quit.txt %}
{% endcapture %}

{% capture blueprint_example_flush_and_quit %}
<img class="posthog-blueprint-flushing-on-quit" src="{{ '/assets/images/blueprints/flushing_on_quit.webp' | relative_url }}" alt="A Blueprint graph that gets the PostHog Runtime Subsystem and calls Flush And Quit before exiting the game.">
{% include blueprint-copy.html id="flushing-on-quit" text=blueprint_example_flush_and_quit_graph %}
{% endcapture %}

{% include language-toggle.html id="lifecycle-flush-and-quit" cpp=cpp_example_flush_and_quit blueprint_text=blueprint_example_flush_and_quit %}

{: .note}
> `FlushAndQuit` does not guarantee network delivery. If a queued event cannot be delivered before shutdown, UnrealHog keeps it in persistent storage and can send it during a later consented run.

{% include page-footer.html title="Reliability and Diagnostics" url="/reliability-and-diagnostics.html" %}
