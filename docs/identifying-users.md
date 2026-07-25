---
layout: default
title: Identifying Users
parent: Analytics
nav_order: 2
---

# Identifying Users

UnrealHog assigns an anonymous distinct ID to a player before they sign in. Once you know who the player is, you can identify them with a stable ID so their earlier anonymous activity and future activity belong to the same person in PostHog.

{: .note-title}
> This requires opt-in
>
> Identity changes and identity events are ignored until the user has opted in. See [User Opt-In Status](user-opt-in-status.html) before relying on identified activity.

## Choose A Stable Identifier

Use an opaque, stable, unique ID from your account system. Good values are immutable internal account IDs or a one-way pseudonymous identifier designed for analytics.

Do not use display names, email addresses, or other values that can change. Do not use sentinel values such as `"null"`, `"unknown"`, or `"guest"`: different players given the same value can be merged into one [PostHog person](https://posthog.com/docs/data/persons). Avoid putting personal data in an identifier when an opaque ID is sufficient.

UnrealHog generates the anonymous distinct ID itself. You do not need to identify a guest with that value; anonymous events already use it.

## Identify After Authentication

Call `Identify` after consent and after sign-in or account creation succeeds. A typical account lifecycle is:

1. Apply the player's analytics choice with `SetAnalyticsOptIn`.
2. If opted in and authenticated, call `Identify` once for the relevant login/game session.
3. Apply account-dependent groups and super properties.
4. Capture account activity.
5. On sign-out or account switch, clear account-sensitive super properties and call `Reset`.
6. Identify the next account and reapply its context only after authentication succeeds.

Do not call `Identify` for every event or from a Tick function. Blank or whitespace-only IDs are rejected and leave the current identity unchanged.

You can provide properties to update the PostHog person profile. `UserProperties` sets or replaces supplied values, while `UserPropertiesSetOnce` initializes values that are not already present. Whether PostHog processes person profiles depends on [**Person Profiles**](configuration.html#user-profiles):

| Setting | Behavior |
|:--|:--|
| `Always` | Process profiles for anonymous and identified events. |
| `Identified Only` | Process profiles after `Identify`; this is the default. |
| `Never` | Do not process person profiles. |

{% include api-method.md id="identify" %}

{% capture cpp_example_identify %}
```cpp
#include "Engine/GameInstance.h"
#include "Events/PostHogEventProperties.h"
#include "Subsystems/PostHogRuntimeSubsystem.h"
// ...
UPostHogRuntimeSubsystem* PostHog = GetGameInstance()->GetSubsystem<UPostHogRuntimeSubsystem>();
check(PostHog); // Optional

UPostHogEventProperties* UserProperties = PostHog->CreateEventProperties();
UserProperties->AddString(TEXT("membership_status"), TEXT("pro"));
UserProperties->AddBoolean(TEXT("email_verified"), true);

UPostHogEventProperties* UserPropertiesSetOnce = PostHog->CreateEventProperties();
UserPropertiesSetOnce->AddString(TEXT("first_seen_platform"), TEXT("Win64"));

// Retrieve this opaque, stable ID from your authenticated account system.
PostHog->Identify(TEXT("example-player-id"), UserProperties, UserPropertiesSetOnce);
```
{% endcapture %}

{% capture blueprint_example_identify_graph %}
{% include blueprints/identify-spaghetti.txt %}
{% endcapture %}

{% capture blueprint_example_identify %}
<img class="posthog-blueprint-identify" src="{{ '/assets/images/blueprints/identify_spaghetti.webp' | relative_url }}" alt="A Blueprint graph that creates user properties and set-once user properties before calling Identify on the PostHog Runtime Subsystem.">
{% include blueprint-copy.html id="identify-user" text=blueprint_example_identify_graph %}
{% endcapture %}

{% include language-toggle.html id="identify-user" cpp=cpp_example_identify blueprint_text=blueprint_example_identify %}

See [Building Event Properties](capturing-events.html#building-event-properties) for all supported property types.

## Reading The Current Distinct ID

`GetDistinctId` returns the ID that UnrealHog will attach to new events. It returns the generated anonymous ID before identification and the supplied user ID afterward.

{% include api-method.md id="get-distinct-id" %}

{% capture cpp_example_get_distinct_id %}
```cpp
#include "Engine/GameInstance.h"
#include "Subsystems/PostHogRuntimeSubsystem.h"
// ...
UPostHogRuntimeSubsystem* PostHog = GetGameInstance()->GetSubsystem<UPostHogRuntimeSubsystem>();
check(PostHog); // Optional
// ...
const FString CurrentDistinctId = PostHog->GetDistinctId();
```
{% endcapture %}

{% capture blueprint_example_get_distinct_id_graph %}
{% include blueprints/get-distinct-id.txt %}
{% endcapture %}

{% capture blueprint_example_get_distinct_id %}
<img class="posthog-blueprint-get-distinct-id" src="{{ '/assets/images/blueprints/get_distinct_id.webp' | relative_url }}" alt="A Blueprint graph that gets the current distinct ID from the PostHog Runtime Subsystem.">
{% include blueprint-copy.html id="get-distinct-id" text=blueprint_example_get_distinct_id_graph %}
{% endcapture %}

{% include language-toggle.html id="get-distinct-id" cpp=cpp_example_get_distinct_id blueprint_text=blueprint_example_get_distinct_id %}

## Creating An Alias

An alias connects an alternate identifier to the **current** distinct ID. The call emits the equivalent of “make `AliasId` an alias of the current player.” This can be useful for a carefully planned ID migration or for linking a legacy identifier.

For ordinary login, use `Identify`, not `Alias`. Aliasing is an advanced operation because the direction and history of both IDs affect person merging. Confirm the current distinct ID with `GetDistinctId`, and ensure the alias has not already been associated with another person using `Identify`.

Blank or whitespace-only alias IDs are rejected.

{% include api-method.md id="alias" %}

{% capture cpp_example_alias %}
```cpp
#include "Engine/GameInstance.h"
#include "Subsystems/PostHogRuntimeSubsystem.h"
// ...
UPostHogRuntimeSubsystem* PostHog = GetGameInstance()->GetSubsystem<UPostHogRuntimeSubsystem>();
check(PostHog); // Optional
// ...
PostHog->Alias(TEXT("legacy-example-account"));
```
{% endcapture %}

{% capture blueprint_example_alias_graph %}
{% include blueprints/alias.txt %}
{% endcapture %}

{% capture blueprint_example_alias %}
<img class="posthog-blueprint-alias" src="{{ '/assets/images/blueprints/alias.webp' | relative_url }}" alt="A Blueprint graph that calls Alias on the PostHog Runtime Subsystem with a legacy account ID.">
{% include blueprint-copy.html id="alias-user" text=blueprint_example_alias_graph %}
{% endcapture %}

{% include language-toggle.html id="alias-user" cpp=cpp_example_alias blueprint_text=blueprint_example_alias %}

## Resetting Identity

Call `Reset` whenever a player signs out or before another player begins using a shared device. This prevents later activity from being attributed to the previous account.

`Reset`:

- Clears the identified distinct ID and all group memberships.
- Returns to an anonymous identity.
- Starts a new analytics session.
- Persists the new identity state.

It does **not** clear consent, queued events that were already composed, registered super properties, or lifecycle state. Already-queued events keep the identity and groups they had at capture time.

{: .warning-title}
> Clear account context during sign-out
>
> Super properties survive `Reset`, opt-out/opt-in, and later launches. Call `ClearSuperProperties` or unregister each account-sensitive key before another account can generate events. Re-register only the context appropriate for the new account.

{% include api-method.md id="reset" %}

{% capture cpp_example_reset %}
```cpp
#include "Engine/GameInstance.h"
#include "Subsystems/PostHogRuntimeSubsystem.h"
// ...
UPostHogRuntimeSubsystem* PostHog = GetGameInstance()->GetSubsystem<UPostHogRuntimeSubsystem>();
check(PostHog); // Optional
// ...
PostHog->Reset();
```
{% endcapture %}

{% capture blueprint_example_reset_graph %}
{% include blueprints/reset.txt %}
{% endcapture %}

{% capture blueprint_example_reset %}
<img class="posthog-blueprint-reset" src="{{ '/assets/images/blueprints/reset.webp' | relative_url }}" alt="A Blueprint graph that calls Reset on the PostHog Runtime Subsystem as part of an account sign-out flow.">
{% include blueprint-copy.html id="reset-identity" text=blueprint_example_reset_graph %}
{% endcapture %}

{% include language-toggle.html id="reset-identity" cpp=cpp_example_reset blueprint_text=blueprint_example_reset %}

{: .note}
> [**Reuse Anonymous ID**](configuration.html#identity) defaults to `false`, so `Reset` generates a new anonymous ID. When enabled, `Reset` restores the anonymous ID that existed before identification. Enable it only when that continuity matches your shared-device and privacy model.

{% include page-footer.html title="Super Properties" url="/super-properties.html" %}
