---
layout: default
title: Capturing Events
parent: Analytics
nav_order: 1
---

# Capturing Events

Capturing events is how you queue an event to be sent to your PostHog instance. The only thing required to capture events is an event name, but you can optionally attach custom properties to the event.

{: .note-title}
> This requires opt-in
>
> No event payload, queue record, file, or HTTP request is created until the user has opted in. See [User Opt-In Status](user-opt-in-status.html) to check or change that state before relying on captured events.

## Capture A Custom Event

`CaptureEvent` queues a custom named event; `Properties` is optional data you can attach to the event payload.

{: .note}
> PostHog suggests naming your events in `[subject]_[verb]` format, where the subject is whatever is being affected in this event, and verb is a (usually past tense) explanation of the action being taken on that subject.
>
> For example: `chest_opened`, `door_unlocked`, or `boss_defeated`.

{% include api-method.md id="capture-event" %}

{% capture cpp_example_capture %}
```cpp
#include "Engine/GameInstance.h" // Required to get the full type of UGameInstance.
#include "Subsystems/PostHogRuntimeSubsystem.h"
// ...
UPostHogRuntimeSubsystem* PostHog = GetGameInstance()->GetSubsystem<UPostHogRuntimeSubsystem>();
check(PostHog); // Optional
// ...
PostHog->CaptureEvent(TEXT("door_unlocked"));
// ...

```
{% endcapture %}

{% capture blueprint_capture_event_graph %}
{% include blueprints/capture-event.txt %}
{% endcapture %}

{% capture blueprint_capture %}
<img class="posthog-blueprint-event-capture" src="{{ '/assets/images/blueprints/catpure_event_no_properties.webp' | relative_url }}" alt="The PostHog Runtime Subsystem node attached to a Capture Event node with no properties inputted.">
{% include blueprint-copy.html id="capture-event" text=blueprint_capture_event_graph %}
{% endcapture %}

{% include language-toggle.html id="capture-event" cpp=cpp_example_capture blueprint_text=blueprint_capture %}

## Building Event Properties

When capturing any sort of event, whether it is a custom event in `Capture Event`, or an already-defined PostHog event like `Capture Screen` or `Identify`, you can set an optional payload of properties to attach to any given event.

You start by creating this payload through the Subsystem's `Create Event Properties` helper function.

{% include api-method.md id="create-event-properties" %}

{% capture cpp_example_create_properties %}
```cpp
#include "Engine/GameInstance.h" // Required to get the full type of UGameInstance.
#include "Events/PostHogEventProperties.h"
#include "Subsystems/PostHogRuntimeSubsystem.h"
// ...
UPostHogRuntimeSubsystem* PostHog = GetGameInstance()->GetSubsystem<UPostHogRuntimeSubsystem>();
check(PostHog); // Optional
// ...
UPostHogEventProperties* Props = PostHog->CreateEventProperties();
// ...

```
{% endcapture %}

{% capture blueprint_create_event_properties_graph %}
{% include blueprints/create-event-properties.txt %}
{% endcapture %}

{% capture blueprint_create_properties %}
<img class="posthog-blueprint-create-event-properties" src="{{ '/assets/images/blueprints/create_event_properties.webp' | relative_url }}" alt="The PostHog Runtime Subsystem node attached to a Create Event Properties node.">
{% include blueprint-copy.html id="create-event-properties" text=blueprint_create_event_properties_graph %}
{% endcapture %}

{% include language-toggle.html id="create-properties" cpp=cpp_example_create_properties blueprint_text=blueprint_create_properties %}

{: .note}
> The PostHog Event Properties maps to a dictionary of JSON's primary data types, `string`, `number`, `boolean`, `null`, `object`, and `array`.
>
> `Object` represents a nested `PostHog Event Properties` object, while `array` is an array of JSON's primary data types, created through the subsystem's helper function [`Create Event Property Array`](api-events-and-properties.html#create-event-property-array).

<details markdown="1">
<summary>Event Property & Event Property Array Add Data Methods</summary>
{% include api-method.md id="add-string" %}

{% include api-method.md id="add-number" %}

{% include api-method.md id="add-boolean" %}

{% include api-method.md id="add-null" %}

{% include api-method.md id="add-object" %}

{% include api-method.md id="add-array" %}
</details>


{% capture cpp_example_door_unlocked_properties %}
```cpp
#include "Engine/GameInstance.h" // Required to get the full type of UGameInstance.
#include "Events/PostHogEventProperties.h"
#include "Subsystems/PostHogRuntimeSubsystem.h"
// ...
UPostHogRuntimeSubsystem* PostHog = GetGameInstance()->GetSubsystem<UPostHogRuntimeSubsystem>();
check(PostHog); // Optional
// ...
UPostHogEventProperties* Props = PostHog->CreateEventProperties();
Props->AddString(TEXT("dungeon"), TEXT("Wind Temple"));
Props->AddBoolean(TEXT("used_magic_key"), false);
Props->AddNumber(TEXT("door_id"), 3);

PostHog->CaptureEvent(TEXT("door_unlocked"), Props);
// ...

```
{% endcapture %}


{% capture blueprint_example_event_properties_graph %}
{% include blueprints/example-event-properties.txt %}
{% endcapture %}

{% capture blueprint_door_unlocked_properties %}
<img class="posthog-blueprint-create-event-properties" src="{{ '/assets/images/blueprints/add_properties.webp' | relative_url }}" alt="A PostHogEventProperties being created and populated with 'dungeon', 'used_magic_key', and 'door_id' properties, before being sent attached to a 'door_unlocked' event.">
{% include blueprint-copy.html id="example-event-properties" text=blueprint_example_event_properties_graph %}
{% endcapture %}

{% include language-toggle.html id="build-properties" cpp=cpp_example_door_unlocked_properties blueprint_text=blueprint_door_unlocked_properties %}

## Capturing Screens

PostHog has several special events that give you extra functionality. The `$screen` event is meant to track your users navigation through your game.

Tracking where your player goes while playing your game can provide invaluable insight on how users are navigating and experiencing content in your game. The `Capture Screen` handles sending this event for you.

{% include api-method.md id="capture-screen" %}

{% capture cpp_example_capture_screen %}
```cpp
#include "Engine/GameInstance.h" // Required to get the full type of UGameInstance.
#include "Subsystems/PostHogRuntimeSubsystem.h"
// ...
UPostHogRuntimeSubsystem* PostHog = GetGameInstance()->GetSubsystem<UPostHogRuntimeSubsystem>();
check(PostHog); // Optional
// ...
// Make any props you think would be useful in this context.
// ...
PostHog->CaptureScreen(TEXT("inventory_weapons"), Props);
// ...

```
{% endcapture %}

{% capture blueprint_capture_screen_graph %}
{% include blueprints/capture-screen.txt %}
{% endcapture %}

{% capture blueprint_capture_screen %}
<img class="posthog-blueprint-capture-screen" src="{{ '/assets/images/blueprints/capture_screen.webp' | relative_url }}" alt="A Blueprint graph that gets the PostHog Runtime Subsystem and calls Capture Screen with the screen name inventory_weapons.">
{% include blueprint-copy.html id="capture-screen" text=blueprint_capture_screen_graph %}
{% endcapture %}

{% include language-toggle.html id="capture-screen" cpp=cpp_example_capture_screen blueprint_text=blueprint_capture_screen %}

## Flushing Queued Events

Events are queued rather than sent immediately. `Flush` requests an asynchronous drain and returns right away with whether the request was accepted; it does not block the calling thread and is safe to call at any time, including before consent, before initialization, or during shutdown.

{% include api-method.md id="flush" %}

{% include api-method.md id="flush-with-completion" %}

{% capture cpp_example_flush %}
```cpp
#include "Engine/GameInstance.h" // Required to get the full type of UGameInstance.
#include "Subsystems/PostHogRuntimeSubsystem.h"
// ...
UPostHogRuntimeSubsystem* PostHog = GetGameInstance()->GetSubsystem<UPostHogRuntimeSubsystem>();
check(PostHog); // Optional
// ...
PostHog->Flush();
```
<details markdown="1">
<summary>Binding to the Flush Completed Delegate</summary>

If you want to respond to when the flush completes, you can use a delegate.

```cpp
// Use CreateWeakLambda(this, [](EPostHogFlushOutcome Outcome){}) if the lambda needs 'this' UObject.
// The weak lambda runs only while 'this' is still valid.
PostHog->Flush(FPostHogFlushCompletedDelegate::CreateWeakLambda(this, [](EPostHogFlushOutcome Outcome)
{
	// Do stuff
}));

// Use CreateLambda([](EPostHogFlushOutcome Outcome){}) if you don't need 'this' UObject inside of the lambda.
PostHog->Flush(FPostHogFlushCompletedDelegate::CreateLambda([](EPostHogFlushOutcome Outcome)
{
	// Do stuff
}));

// Use CreateUObject(this, &AMyActor::MemberFunction) if you want to use a member function.
// Assume the signature: void AMyActor::MemberFunction(EPostHogFlushOutcome Outcome)
PostHog->Flush(FPostHogFlushCompletedDelegate::CreateUObject(this, &AMyActor::MemberFunction));
```
</details>
{% endcapture %}

{% capture blueprint_manual_flush_graph %}
{% include blueprints/manual-flush.txt %}
{% endcapture %}

{% capture blueprint_text_flush %}
<img class="posthog-blueprint-capture-screen" src="{{ '/assets/images/blueprints/flush.webp' | relative_url }}" alt="A Blueprint graph that gets the PostHog Runtime Subsystem and calls Flush with it.">
{% include blueprint-copy.html id="manual-flush" text=blueprint_manual_flush_graph %}
{% endcapture %}

{% include language-toggle.html id="flush" cpp=cpp_example_flush blueprint_text=blueprint_text_flush %}

{% include page-footer.html title="Identifying Users" url="/identifying-users.html" %}
