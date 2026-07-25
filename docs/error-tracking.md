---
layout: default
title: Error Tracking
parent: Reliability and Diagnostics
nav_order: 3
---

# Error Tracking

UnrealHog can capture Unreal `ensure` failures automatically, and you can report handled failures manually when your own code has better context. Captured failures appear as exception events in PostHog.

{: .warning-title}
> Automatic capture is intentionally limited
>
> UnrealHog `{{ site.unrealhog_version }}` automatically listens only for Unreal `ensure` failures. It does not intercept C++ exceptions, fatal crashes, `check` failures, arbitrary Unreal log errors, operating-system crash reports, or GPU/device failures. Use a dedicated crash-reporting solution for process-ending and platform-native failures.

{: .note-title}
> This requires opt-in
>
> Automatic handlers are registered only while [**Capture Exceptions**](configuration.html#exception-tracking) is enabled and analytics is opted in. Opting out removes the handler; opting in registers it again. Manual exceptions are also ignored without opt-in. See [User Opt-In Status](user-opt-in-status.html).

## Automatic Error Tracking

Open **Edit > Project Settings > Analytics > PostHog** and use the **Exception Tracking** section to configure automatic capture:

- `Capture Exceptions` enables automatic capture and defaults to `true`.
- `Capture Exceptions in Editor` allows capture while running in the Unreal Editor and defaults to `true`.
- `Exception Debounce Interval Ms` suppresses repeated reports within the configured interval. It defaults to `1000`; set it to `0` to disable debouncing.

Because Editor capture defaults to enabled, point development builds at a separate PostHog project or disable **Capture Exceptions in Editor** when Editor failures should not enter analytics.

Whether an `ensure` exists in a packaged binary depends on the Unreal target configuration:

- Debug and Development targets normally compile ensures in.
- Test and Shipping targets normally compile them out unless the project's build settings enable ensures/checks in Shipping.
- A regular `ensure` normally reports only its first failure at a call site during that process. `ensureAlways` can report repeatedly, after which UnrealHog's global debounce interval can still suppress closely spaced reports.

Build rules can override these defaults. Verify the behavior of the exact packaged target you distribute. When compiled out, the ensure expression can still be evaluated, but UnrealHog does not receive an `OnEnsureFailed` notification.

The automatic report type is `Ensure`. Its current automatic stack information is the source file and line supplied by Unreal's ensure delegate, not a complete native crash stack.

## Design Useful And Safe Reports

Keep `Type` stable across occurrences, such as `InventorySyncError`. Write a concise `Message` that describes the failed operation, and put variable values such as retry count, game mode, or backend response class in custom properties. Stable types plus useful messages and frames make reports easier to search and compare without creating a new category for every runtime value.

{: .warning-title}
> Exception data can be sensitive
>
> Messages, custom properties, stack frames, function names, and file paths can expose player data, developer usernames, machine paths, internal service names, or tokens. Sanitize inputs before capture, avoid embedding raw request/response bodies, and verify Editor and packaged-build payloads in a development PostHog project. A C++ [Before Send](before-send.html) hook can provide an additional redaction layer.

## Capturing An Exception Manually

Use `CaptureException` for handled failures that you want to report yourself. The exception input contains:

| Field | Required | Description |
|:------|:---------|:------------|
| `Message` | Yes | A human-readable description of the failure. |
| `Type` | Yes | A stable category for the failure, such as `InventorySyncError`. |
| `StackTrace` | No | Raw stack trace text. Separate frames with newlines when possible. |
| `bHandled` | No | Whether application code handled the failure. Defaults to `true`. |

Both `Message` and `Type` must contain non-whitespace text. You can also attach custom event properties to provide useful context.

{: .note}
> `CaptureException` expects you to provide a complete exception input. When using it directly, build the exception struct yourself, including `StackTrace` if you want stack frames attached. If you want UnrealHog to capture the current stack for you, use `MakeExceptionWithCurrentStack` below.

{% include api-method.md id="capture-exception" %}

{% capture cpp_example_capture_exception %}
```cpp
#include "Engine/GameInstance.h"
#include "Events/PostHogEventProperties.h"
#include "Events/PostHogExceptionInput.h"
#include "Subsystems/PostHogRuntimeSubsystem.h"
// ...
UPostHogRuntimeSubsystem* PostHog = GetGameInstance()->GetSubsystem<UPostHogRuntimeSubsystem>();
check(PostHog); // Optional
// ...
FPostHogExceptionInput Exception;
Exception.Message = TEXT("Inventory sync failed");
Exception.Type = TEXT("InventorySyncError");
Exception.StackTrace = TEXT("UMyInventoryService::Sync\nUMyInventoryService::Refresh");
Exception.bHandled = true;

UPostHogEventProperties* Properties = PostHog->CreateEventProperties();
Properties->AddString(TEXT("system"), TEXT("inventory"));
Properties->AddNumber(TEXT("retry_count"), 3);

PostHog->CaptureException(Exception, Properties);
```
{% endcapture %}

{% capture blueprint_example_capture_exception_graph %}
{% include blueprints/capture-exception-manually.txt %}
{% endcapture %}

{% capture blueprint_example_capture_exception %}
<img class="posthog-blueprint-capture-exception" src="{{ '/assets/images/blueprints/capture_exception_manually.webp' | relative_url }}" alt="A Blueprint graph that creates a handled inventory exception, adds event properties, and captures the exception on the PostHog Runtime Subsystem.">
{% include blueprint-copy.html id="capture-exception-manually" text=blueprint_example_capture_exception_graph %}
{% endcapture %}

{% include language-toggle.html id="capture-exception" cpp=cpp_example_capture_exception blueprint_text=blueprint_example_capture_exception %}

## Capturing The Current Stack Trace

For most manual reports, use `MakeExceptionWithCurrentStack` from `UPostHogExceptionLibrary`. It builds the `FPostHogExceptionInput` for you, including `Message`, `Type`, `bHandled`, and a stack trace captured at the call site. Build the struct manually only when you already have more authoritative stack text or deliberately do not want a stack.

Call it as close to the failure as possible. The stack represents where the helper runs, not where an earlier error object was created. Blueprints capture the active Blueprint execution stack, while C++ captures the native call stack.

{% capture cpp_example_capture_current_stack %}
```cpp
#include "Engine/GameInstance.h"
#include "Events/PostHogEventProperties.h"
#include "Events/PostHogExceptionInput.h"
#include "Subsystems/PostHogRuntimeSubsystem.h"
#include "Utilities/PostHogExceptionLibrary.h"
// ...
UPostHogRuntimeSubsystem* PostHog = GetGameInstance()->GetSubsystem<UPostHogRuntimeSubsystem>();
check(PostHog); // Optional
// ...
FPostHogExceptionInput Exception = UPostHogExceptionLibrary::MakeExceptionWithCurrentStack(
	TEXT("Inventory sync failed"),
	TEXT("InventorySyncError"));

UPostHogEventProperties* Properties = PostHog->CreateEventProperties();
Properties->AddString(TEXT("system"), TEXT("inventory"));

PostHog->CaptureException(Exception, Properties);
```

{: .note}
> Native stack capture is best-effort. Optimized builds may omit or merge source-level functions because of inlining, tail-call optimization, frame-pointer omission, wrapper folding, symbol availability, or optimizer removal and reordering of trivial call boundaries. Even with `FORCENOINLINE`, an intermediate function can disappear in optimized builds and reappear in `DebugGame`. Treat native stacks as diagnostic hints, not an exact source call graph.
{% endcapture %}

{% capture blueprint_example_capture_current_stack_graph %}
{% include blueprints/capture-exception-manually-with-trace.txt %}
{% endcapture %}

{% capture blueprint_example_capture_current_stack %}
<img class="posthog-blueprint-capture-exception-current-stack" src="{{ '/assets/images/blueprints/capture_exception_manually_with_trace.webp' | relative_url }}" alt="A Blueprint graph that creates an exception with the current stack trace, adds event properties, and captures the exception on the PostHog Runtime Subsystem.">
{% include blueprint-copy.html id="capture-exception-manually-with-trace" text=blueprint_example_capture_current_stack_graph %}
{% endcapture %}

{% include language-toggle.html id="capture-current-stack" cpp=cpp_example_capture_current_stack blueprint_text=blueprint_example_capture_current_stack %}

If you set `StackTrace` manually or use an explicit-stack helper, UnrealHog preserves your supplied stack text and order.

See [Building Event Properties](capturing-events.html#building-event-properties) for all supported custom property types.

## UnrealHog Versus Crash Reporting

| UnrealHog error events | Dedicated crash reporting |
|:--|:--|
| Consent-gated analytics events for `ensure` failures and manually reported handled failures. | Captures process-ending crashes, platform crash artifacts, minidumps, and native crash context. |
| Delivered through the normal durable analytics queue while the process can continue. | Designed to collect data during or after abnormal termination. |
| Useful for connecting recoverable failures with product analytics context. | Best source for crash rates, symbols, native stacks, and platform diagnostics. |

Many games need both tools. Do not assume an UnrealHog event will be delivered after a fatal failure.

{% include page-footer.html title="Advanced" url="/advanced.html" %}
