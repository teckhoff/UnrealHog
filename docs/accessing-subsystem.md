---
layout: default
title: Accessing the Subsystem
parent: Getting Started
nav_order: 4
---

# Accessing The Subsystem

The PostHog Runtime Subsystem is the public-facing API you will be using to access all PostHog-related functionality.

The PostHog Runtime Subsystem is a [GameInstance Subsystem](https://dev.epicgames.com/documentation/unreal-engine/programming-subsystems-in-unreal-engine?lang=en-US) that gets created when the game boots and the player's [GameInstance](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UGameInstance?lang=en-US) is created.

{% capture cpp_example_access %}
The type for the subsystem is located in `Subsystems/PostHogRuntimeSubsystem.h`.

Grabbing a reference to the subsystem is as easy as getting the current GameInstance, and calling `GetSubsystem<UPostHogRuntimeSubsystem>()` on it.

```cpp
#include "Engine/GameInstance.h" // Required to get the full type of UGameInstance.
#include "Subsystems/PostHogRuntimeSubsystem.h"

UPostHogRuntimeSubsystem* PostHog = GetGameInstance()->GetSubsystem<UPostHogRuntimeSubsystem>();
check(PostHog); // Optional
```

{: .note}
> Because the subsystem's lifetime is bound to a GameInstance, there should be no possibility for this to be null. You can still opt into using `check(PostHog)` to ensure it exists during development. This check gets stripped out of Shipping builds.
>
{% endcapture %}

{% capture blueprint_example_access %}
You can grab the PostHog Runtime Subsystem the same way you would any other subsystem - opening the context menu and searching for the subsystem by name.

<img class="posthog-settings-display" src="{{ '/assets/images/blueprints/grab_posthog_subsystem.webp' | relative_url }}" alt="The Unreal Engine Blueprint context menu showing 'Get PostHogRuntimeSubsystem'.">
{% endcapture %}

{% include language-toggle.html id="accessing-subsystem" cpp=cpp_example_access blueprint_text=blueprint_example_access %}

{% include page-footer.html title="User Opt-In Status" url="/user-opt-in-status.html" %}
