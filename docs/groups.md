---
layout: default
title: Groups
parent: Analytics
nav_order: 4
---

# Groups

{: .warning}
> Group analytics is a paid PostHog add-on. Billing begins when you subscribe to group analytics from PostHog's billing page, not when you add a `Group` call to your game. Once enabled, all identified events in the project count toward group analytics billing, including events without group properties. [See PostHog's current billing guidance](https://posthog.com/docs/product-analytics/group-analytics#billing).

Groups let you associate events with an organization, guild, team, account, or another shared entity. PostHog can then analyze activity by that group instead of only by individual users.

{: .note-title}
> This requires opt-in
>
> Group membership and group events are not created or persisted until the user has opted in. See [User Opt-In Status](user-opt-in-status.html) before assigning groups.

## Assigning A Group

`GroupType` names the kind of group, such as `alliance`, while `GroupKey` identifies the particular group. After `Group` is called, UnrealHog adds that membership to future events.

You can optionally include properties that describe the group itself. Calling `Group` again with the same group type replaces that type's current group key.

{% include api-method.md id="group" %}

{% capture cpp_example_group %}
```cpp
#include "Engine/GameInstance.h"
#include "Events/PostHogEventProperties.h"
#include "Subsystems/PostHogRuntimeSubsystem.h"
// ...
UPostHogRuntimeSubsystem* PostHog = GetGameInstance()->GetSubsystem<UPostHogRuntimeSubsystem>();
check(PostHog); // Optional
// ...
UPostHogEventProperties* GuildProperties = PostHog->CreateEventProperties();
GuildProperties->AddNumber(TEXT("member_count"), 30);

PostHog->Group(TEXT("alliance"), TEXT("New Alberia"), GuildProperties);
```
{% endcapture %}

{% capture blueprint_example_group_graph %}
{% include blueprints/assigning-a-group.txt %}
{% endcapture %}

{% capture blueprint_example_group %}
<img class="posthog-blueprint-assigning-a-group" src="{{ '/assets/images/blueprints/assigning_a_group.webp' | relative_url }}" alt="A Blueprint graph that builds group properties and assigns the player to the New Alberia alliance group on the PostHog Runtime Subsystem.">
{% include blueprint-copy.html id="assigning-a-group" text=blueprint_example_group_graph %}
{% endcapture %}

{% include language-toggle.html id="assign-group" cpp=cpp_example_group blueprint_text=blueprint_example_group %}

See [Building Event Properties](capturing-events.html#building-event-properties) for all supported group property types.

## Clearing Groups

`ResetGroups` clears every current group membership. Call it when group context should no longer be attached to future events.

{% include api-method.md id="reset-groups" %}

{% capture cpp_example_reset_groups %}
```cpp
#include "Engine/GameInstance.h"
#include "Subsystems/PostHogRuntimeSubsystem.h"
// ...
UPostHogRuntimeSubsystem* PostHog = GetGameInstance()->GetSubsystem<UPostHogRuntimeSubsystem>();
check(PostHog); // Optional
// ...
PostHog->ResetGroups();
```
{% endcapture %}

{% capture blueprint_example_reset_groups_graph %}
{% include blueprints/reset-groups.txt %}
{% endcapture %}

{% capture blueprint_example_reset_groups %}
<img class="posthog-blueprint-reset-groups" src="{{ '/assets/images/blueprints/reset-groups.webp' | relative_url }}" alt="A Blueprint graph that resets all group memberships on the PostHog Runtime Subsystem.">
{% include blueprint-copy.html id="reset-groups" text=blueprint_example_reset_groups_graph %}
{% endcapture %}

{% include language-toggle.html id="reset-groups" cpp=cpp_example_reset_groups blueprint_text=blueprint_example_reset_groups %}

{: .note}
> Calling [`Reset`](identifying-users.html#resetting-identity) during sign-out also clears the current group memberships.

{% include page-footer.html title="Application Lifecycle Events" url="/application-lifecycle-events.html" %}
