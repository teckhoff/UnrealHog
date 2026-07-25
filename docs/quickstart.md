---
layout: default
title: Quickstart
parent: Getting Started
nav_order: 2
---

# Quickstart

This guide sends one event from Unreal Engine 5.8 to a development PostHog project. It assumes that you have [installed and enabled UnrealHog](installation.html).

## 1. Configure the project

In PostHog, copy the **Project API Key** for the development project that should receive the event. This is the public project token, usually beginning with `phc_`; it is not a personal API key.

In Unreal Editor:

1. Open **Edit > Project Settings > Analytics > PostHog**.
2. Paste the token into **Project Public API Key**.
3. Choose the matching host:
   - **US** for `https://us.i.posthog.com`.
   - **EU** for `https://eu.i.posthog.com`.
   - **Custom** for a self-hosted or reverse-proxied PostHog ingestion URL.
4. Ensure **Analytics Enabled** is selected.

See the [Configuration Reference](configuration.html) for every setting and default.

## 2. Capture and flush a test event

The quickstart opts in explicitly because UnrealHog collects nothing until consent is granted. Use this only in a development verification path. A production game should call `SetAnalyticsOptIn` from its real consent UI.

{% capture cpp_quickstart %}
Add `"UnrealHog"` to your game module's private dependencies, then call this from an object with a valid game instance:

```cpp
#include "Engine/GameInstance.h"
#include "Events/PostHogEventProperties.h"
#include "Subsystems/PostHogRuntimeSubsystem.h"

void AMyPlayerController::SendUnrealHogQuickstartEvent()
{
	UGameInstance* GameInstance = GetGameInstance();
	if (!IsValid(GameInstance))
	{
		return;
	}

	UPostHogRuntimeSubsystem* PostHog =
		GameInstance->GetSubsystem<UPostHogRuntimeSubsystem>();
	if (!IsValid(PostHog))
	{
		return;
	}

	// Development verification only. In production, use the player's real choice.
	PostHog->SetAnalyticsOptIn(true);
	if (!PostHog->IsAnalyticsOptedIn())
	{
		UE_LOG(LogTemp, Warning, TEXT("UnrealHog could not opt in. Check project settings."));
		return;
	}

	UPostHogEventProperties* Properties = PostHog->CreateEventProperties();
	Properties->AddString(TEXT("source"), TEXT("unrealhog_quickstart"));

	PostHog->CaptureEvent(TEXT("unrealhog_quickstart_completed"), Properties);

	const EPostHogFlushRequestResult FlushResult = PostHog->Flush();
	UE_LOG(LogTemp, Display, TEXT("UnrealHog flush request: %d"),
		static_cast<int32>(FlushResult));
}
```

`Flush()` returns immediately. `Started` or `AlreadyInProgress` means the asynchronous drain was accepted; it does not prove that PostHog received the event.
{% endcapture %}

{% capture blueprint_quickstart_graph %}
{% include blueprints/quickstart.txt %}
{% endcapture %}

{% capture blueprint_quickstart %}
From a Blueprint with a valid game instance:

1. Add **Get Post Hog Runtime Subsystem**.
2. Call **Set Analytics Opt In** with `true`.
3. Call **Is Analytics Opted In** and continue from the `true` branch.
4. Call **Create Event Properties**, then **Add String** with key `source` and value `unrealhog_quickstart`.
5. Call **Capture Event** with event name `unrealhog_quickstart_completed` and the property object.
6. Call **Flush** once to request immediate delivery during verification.

<img class="posthog-blueprint-quickstart" src="{{ '/assets/images/blueprints/quickstart.webp' | relative_url }}" alt="A Blueprint function that opts in for development verification, checks consent, creates the quickstart source property, captures unrealhog_quickstart_completed, flushes the queue, and logs the flush request result.">
{% include blueprint-copy.html id="quickstart-first-event" text=blueprint_quickstart_graph %}

The **Flush** return value reports only whether the request started, joined an existing drain, or was skipped.
{% endcapture %}

{% include language-toggle.html id="quickstart-first-event" cpp=cpp_quickstart blueprint_text=blueprint_quickstart %}

## 3. Verify the event

1. Keep the game running until the manual flush finishes.
2. Open the same PostHog project and inspect its recent events.
3. Search for `unrealhog_quickstart_completed`.
4. Confirm that its `source` property is `unrealhog_quickstart`.

<img class="posthog-quickstart-completed" src="{{ '/assets/images/posthog/unrealhog_quickstart_completed.webp' | relative_url }}" alt="A successfully ingested unrealhog_quickstart_completed event in PostHog showing the source property set to unrealhog_quickstart.">

Ingestion and the PostHog interface are asynchronous, so the event may take a short time to appear. If it does not, work through [Events are not appearing in PostHog](event-delivery-and-troubleshooting.html#events-are-not-appearing-in-posthog).

## 4. Remove the verification code

Do not ship the forced opt-in or quickstart event. Connect `SetAnalyticsOptIn` to the player's actual choice, and let normal batching deliver production events. You should not be calling `Flush()` after every event as this defeats the purpose of batching.

{% include page-footer.html title="Configuration Reference" url="/configuration.html" %}
