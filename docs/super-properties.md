---
layout: default
title: Super Properties
parent: Analytics
nav_order: 3
---

# Super Properties

Super properties are properties that UnrealHog automatically adds to future events. They are useful for values that apply across many events, such as the game mode, build channel, current season, or active experiments.

Registered super properties are stored between launches until you replace, unregister, or clear them.

{: .note-title}
> This requires opt-in
>
> Super properties are not registered, changed, or persisted until the user has opted in. See [User Opt-In Status](user-opt-in-status.html) before setting them.

{: .warning-title}
> Remove account-sensitive values on sign-out
>
> Super properties persist across launches, `Reset`, and opt-out followed by opt-in. Do not store an account ID, entitlement, cohort, or other user-specific value unless your sign-out flow removes it before another player can generate events.

## Registering Super Properties

Just like Event Properties, Super Properties are defined using the JSON primary data types.

Calling a register method again with the same key replaces the stored value for future events. A registration attempted before opt-in is ignored rather than saved for later, so register the required context after consent is granted.

<details markdown="1">
<summary>Register Super Property Methods</summary>
{% include api-method.md id="register-super-property-string" %}

{% include api-method.md id="register-super-property-number" %}

{% include api-method.md id="register-super-property-boolean" %}

{% include api-method.md id="register-super-property-null" %}

{% include api-method.md id="register-super-property-object" %}

{% include api-method.md id="register-super-property-array" %}
</details>

{% capture cpp_example_register_scalars %}
```cpp
#include "Engine/GameInstance.h"
#include "Subsystems/PostHogRuntimeSubsystem.h"
// ...
UPostHogRuntimeSubsystem* PostHog = GetGameInstance()->GetSubsystem<UPostHogRuntimeSubsystem>();
check(PostHog); // Optional
// ...
PostHog->RegisterSuperPropertyString(TEXT("build_channel"), TEXT("shipping"));
PostHog->RegisterSuperPropertyNumber(TEXT("season"), 4);
PostHog->RegisterSuperPropertyBoolean(TEXT("crossplay_enabled"), true);
PostHog->RegisterSuperPropertyNull(TEXT("matchmaking_bucket"));
```
{% endcapture %}

{% capture blueprint_example_register_scalars_graph %}
{% include blueprints/register-super-properties.txt %}
{% endcapture %}

{% capture blueprint_example_register_scalars %}
<img class="posthog-blueprint-register-super-properties" src="{{ '/assets/images/blueprints/register_super_properties.webp' | relative_url }}" alt="A Blueprint graph that registers build channel, season, crossplay enabled, and matchmaking bucket super properties on the PostHog Runtime Subsystem.">
{% include blueprint-copy.html id="register-super-properties" text=blueprint_example_register_scalars_graph %}
{% endcapture %}

{% include language-toggle.html id="register-scalar-super-properties" cpp=cpp_example_register_scalars blueprint_text=blueprint_example_register_scalars %}

Super properties can also contain nested objects and arrays. Build them with `CreateEventProperties` or `CreateEventPropertyArray`, then register the completed value.

{% capture cpp_example_register_structured %}
```cpp
#include "Engine/GameInstance.h"
#include "Events/PostHogEventProperties.h"
#include "Subsystems/PostHogRuntimeSubsystem.h"
// ...
UPostHogRuntimeSubsystem* PostHog = GetGameInstance()->GetSubsystem<UPostHogRuntimeSubsystem>();
check(PostHog); // Optional
// ...
UPostHogEventProperties* Loadout = PostHog->CreateEventProperties();
Loadout->AddString(TEXT("primary"), TEXT("1911"));
Loadout->AddNumber(TEXT("level"), 12);

UPostHogEventPropertyArray* ActiveModifiers = PostHog->CreateEventPropertyArray();
ActiveModifiers->AddString(TEXT("double_xp"));
ActiveModifiers->AddString(TEXT("double_gobblegum"));

PostHog->RegisterSuperPropertyObject(TEXT("loadout"), Loadout);
PostHog->RegisterSuperPropertyArray(TEXT("active_modifiers"), ActiveModifiers);
```
{% endcapture %}

{% capture blueprint_example_register_structured_graph %}
{% include blueprints/register-super-properties-object-array.txt %}
{% endcapture %}

{% capture blueprint_example_register_structured %}
<img class="posthog-blueprint-register-super-properties-object-array" src="{{ '/assets/images/blueprints/register_super_properties_object_array.webp' | relative_url }}" alt="A Blueprint graph that creates a loadout object and active modifiers array, then registers both as super properties.">
{% include blueprint-copy.html id="register-super-properties-object-array" text=blueprint_example_register_structured_graph %}
{% endcapture %}

{% include language-toggle.html id="register-structured-super-properties" cpp=cpp_example_register_structured blueprint_text=blueprint_example_register_structured %}

{: .note}
> Properties passed directly to a captured event override super properties with the same key. Producer-owned properties, such as `$screen_name` and exception fields, are applied afterward, followed by SDK, session, and group properties. PostHog's reserved SDK properties cannot be registered or replaced through normal property builders. See [Data Collected and Stored](data-collected-and-stored.html#event-payloads) for the full composition order.

See [Building Event Properties](capturing-events.html#building-event-properties) for the available object and array value types.

## Removing Super Properties

Use `UnregisterSuperProperty` when one value no longer applies. Use `ClearSuperProperties` when all shared context should be removed, such as when leaving a game mode that registered several related values.

Registering a null value is different from unregistering a key:

- `RegisterSuperPropertyNull` keeps the key and sends an explicit JSON `null` on future events.
- `UnregisterSuperProperty` removes the key so it is absent from future events.
- `ClearSuperProperties` removes every registered key.

These operations affect future event composition only. Events already queued retain their existing property values.

{% include api-method.md id="unregister-super-property" %}

{% include api-method.md id="clear-super-properties" %}

{% capture cpp_example_remove_super_properties %}
```cpp
#include "Engine/GameInstance.h"
#include "Subsystems/PostHogRuntimeSubsystem.h"
// ...
UPostHogRuntimeSubsystem* PostHog = GetGameInstance()->GetSubsystem<UPostHogRuntimeSubsystem>();
check(PostHog); // Optional
// ...
PostHog->UnregisterSuperProperty(TEXT("matchmaking_bucket"));
// ...
// Remove every registered super property.
PostHog->ClearSuperProperties();
```
{% endcapture %}

{% capture blueprint_example_unregister_super_property_graph %}
{% include blueprints/unregister-super-property.txt %}
{% endcapture %}

{% capture blueprint_example_clear_super_properties_graph %}
{% include blueprints/clear-super-properties.txt %}
{% endcapture %}

{% capture blueprint_example_remove_super_properties %}
<img class="posthog-blueprint-unregister-super-property" src="{{ '/assets/images/blueprints/unregister_super_property.webp' | relative_url }}" alt="A Blueprint graph that unregisters the matchmaking bucket super property on the PostHog Runtime Subsystem.">
{% include blueprint-copy.html id="unregister-super-property" text=blueprint_example_unregister_super_property_graph %}

<img class="posthog-blueprint-clear-super-properties" src="{{ '/assets/images/blueprints/clear_super_properties.webp' | relative_url }}" alt="A Blueprint graph that clears every registered super property on the PostHog Runtime Subsystem.">
{% include blueprint-copy.html id="clear-super-properties" text=blueprint_example_clear_super_properties_graph %}
{% endcapture %}

{% include language-toggle.html id="remove-super-properties" cpp=cpp_example_remove_super_properties blueprint_text=blueprint_example_remove_super_properties %}

{% include page-footer.html title="Groups" url="/groups.html" %}
