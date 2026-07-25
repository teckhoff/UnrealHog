---
layout: default
title: User Opt-In Status
parent: Getting Started
nav_order: 5
---

# User Opt-In Status

Your users should have the ability to opt-in and opt-out of analytics at any point.

## Checking Opt-In Status

You can access the users current analytics opt-in status by calling `Is Analytics Opted-In` on the subsystem.

{% include api-method.md id="is-analytics-opted-in" %}

{% capture cpp_example_is_opted %}
```cpp
#include "Engine/GameInstance.h" // Required to get the full type of UGameInstance.
#include "Subsystems/PostHogRuntimeSubsystem.h"

UPostHogRuntimeSubsystem* PostHog = GetGameInstance()->GetSubsystem<UPostHogRuntimeSubsystem>();
check(PostHog); // Optional

bool bIsOptedIn = PostHog->IsAnalyticsOptedIn();

if (bIsOptedIn)
{
    // Do something.
}
```
{% endcapture %}

{% capture blueprint_example_is_opted_graph %}
{% include blueprints/is-opted-in.txt %}
{% endcapture %}

{% capture blueprint_example_is_opted %}
<img class="posthog-settings-display" src="{{ '/assets/images/blueprints/is_opted_in.webp' | relative_url }}" alt="A Blueprint graph that checks Is Analytics Opted In on the PostHog Runtime Subsystem and passes the result to a branch.">
{% include blueprint-copy.html id="is-opted-in" text=blueprint_example_is_opted_graph %}
{% endcapture %}

{% include language-toggle.html id="is-opted-in" cpp=cpp_example_is_opted blueprint_text=blueprint_example_is_opted %}

## Opting In and Opting Out

You can change the player's current Opt-In status with the `Set Analytics Opted-In` function.

{% include api-method.md id="set-analytics-opt-in" %}

{% capture cpp_example_set_opted %}
```cpp
#include "Engine/GameInstance.h" // Required to get the full type of UGameInstance.
#include "Subsystems/PostHogRuntimeSubsystem.h"

void AExamplePlayerController::ToggleOptIn()
{
	UPostHogRuntimeSubsystem* PostHog = GetGameInstance()->GetSubsystem<UPostHogRuntimeSubsystem>();
	check(PostHog);
	
	bool bIsOptedIn = PostHog->IsAnalyticsOptedIn();
	
	PostHog->SetAnalyticsOptIn(!bIsOptedIn);
}
```
{% endcapture %}

{% capture blueprint_example_set_opted_graph %}
{% include blueprints/toggle-opt-in.txt %}
{% endcapture %}

{% capture blueprint_example_set_opted %}
<img class="posthog-settings-display" src="{{ '/assets/images/blueprints/set_opt_in.webp' | relative_url }}" alt="A Blueprint function that reads the current analytics consent state and sets opt-in to the opposite value.">
{% include blueprint-copy.html id="toggle-opt-in" text=blueprint_example_set_opted_graph %}
{% endcapture %}

{% include language-toggle.html id="set-opted-in" cpp=cpp_example_set_opted blueprint_text=blueprint_example_set_opted %}

{: .note-title}
> Privacy and Consent Requirements
>
> Privacy and consent requirements vary by jurisdiction, platform, audience, and the data you collect. UnrealHog defaults to no collection until consent is granted, but this is a technical control rather than legal advice.
>
> Work with qualified counsel to determine your requirements, disclose the data you collect, and connect your consent UI or consent-management system to `SetAnalyticsOptIn`. PostHog also provides general guidance on [controlling data collection](https://posthog.com/docs/privacy/data-collection).
>
> When consent is required, prompt the player at an appropriate point and provide a way to change their choice later, such as in the game's settings menu.

{% include page-footer.html title="Analytics" url="/analytics.html" %}
