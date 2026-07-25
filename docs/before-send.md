---
layout: default
title: Before Send
parent: Advanced
nav_order: 1
---

# Before Send

Before Send is a C++-only hook for inspecting the final composed event immediately before UnrealHog persists it. Use it as a defense-in-depth control to redact fields, normalize values, or intentionally drop selected events.

The hook runs after super properties, per-event properties, special-event fields, SDK context, session, and group data have been applied. Returning:

- `EPostHogBeforeSendResult::Continue` persists the possibly modified event.
- `EPostHogBeforeSendResult::Drop` silently discards the event without creating a queue record.
- `EPostHogBeforeSendResult::Failure` rejects the event without creating a queue record and records an SDK error.

{% include api-method.md id="set-before-send" %}

{% include api-method.md id="clear-before-send" %}

{% include api-enum.md id="posthog-before-send-result" %}

## Redact Or Drop An Event

```cpp
#include "Events/PostHogBeforeSend.h"
#include "Subsystems/PostHogRuntimeSubsystem.h"

// Keep the hook small and deterministic. It runs synchronously during CaptureEvent.
FPostHogBeforeSendDelegate BeforeSend;
BeforeSend.BindWeakLambda(
	this,
	[](FPostHogBeforeSendEvent& Event)
	{
		FJsonObject& Properties = Event.GetMutableProperties();
		Properties.RemoveField(TEXT("email"));
		Properties.RemoveField(TEXT("auth_token"));

		if (Event.GetEventName() == TEXT("internal_diagnostic"))
		{
			return EPostHogBeforeSendResult::Drop;
		}

		return EPostHogBeforeSendResult::Continue;
	});

PostHog->SetBeforeSend(MoveTemp(BeforeSend));
```

Clear the callback when its policy no longer applies:

```cpp
PostHog->ClearBeforeSend();
```

The event view exposes read-only event name, distinct ID, UUID, and timestamp accessors. `GetMutableProperties()` is the supported mutable surface. Changes affect only the event currently being composed.

## Safety And Lifetime Rules

- The callback runs synchronously on the thread that called the capture API. UnrealHog runtime APIs are intended for the game thread; do not block, perform network work, or dispatch asynchronous work from the hook.
- The `FPostHogBeforeSendEvent` and its references are valid only for the duration of the callback. Do not retain their addresses or references.
- The callback stays installed until it is replaced, cleared, or the subsystem is destroyed. Use a weak binding when it depends on a `UObject` lifetime.
- Do not call `CaptureEvent`, `CaptureScreen`, `CaptureException`, or another capture path from the hook. Doing so invokes the hook recursively.
- Avoid changing the hook registration or other SDK state while the hook is executing.
- Treat `Failure` as a local policy or callback failure, not as a retry request. The event is not queued.

{: .warning-title}
> Sanitize as early as possible
>
> Before Send can remove final properties, but it should not be the only control protecting sensitive input. Avoid collecting secrets at the source. For exceptions, sanitize `FPostHogExceptionInput` and custom properties before calling `CaptureException`, because messages and stack frames can contain paths, identifiers, or user data.

See the [Advanced C++ API](api-advanced-cpp.html) for the method signatures and result enum.

{% include page-footer.html title="Packaging the Plugin" url="/packaging.html" %}
