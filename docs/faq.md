---
layout: default
title: FAQ
parent: Reference
nav_order: 3
---

# FAQ

## Why is the plugin called UnrealHog and not PostHog-Unreal?

UnrealHog is not an official SDK maintained by PostHog. If PostHog were to release an official Unreal Engine SDK, they would probably title it `PostHog-Unreal`.

## Is analytics enabled by default?

The developer setting `bAnalyticsEnabled` defaults to `true`, but user consent defaults to opted out through `bDefaultUserOptIn = false`. Events are not collected until opt-in is granted.

## Does error tracking run in the editor?

Exception capture is controlled by `bCaptureExceptions`. Editor capture is separately controlled by `bCaptureExceptionsInEditor`, so teams can disable editor exception reporting while keeping runtime reporting enabled.

## How do lifecycle events work?

Lifecycle capture is controlled by `bCaptureApplicationLifecycleEvents` and still obeys consent. `bFlushOnQuit` and `FlushOnQuitTimeoutSeconds` control the bounded flush window during shutdown, and `FlushAndQuit` performs an explicit drain-then-exit path.

## Why have you released a version of the plugin that does not obey two of the three listed purposes in [Design Considerations](design-considerations.html)?

Event analytics is probably the most important piece of the system in my opinion. Getting that functional is the most important part. Feature flags and session replays, while nice, weren't the core functionality of the SDK.

As for making it Unreal, I'm currently working on defining what features would make it Unreal.

My current plans for doing so are:
- Adding GameplayTags natively into appropriate functionality (defining events; as event properties).
- Create tertiary plugins to bridge some sort of gap (automated GAS analytics, for example).
- Handle local player cases (people still make games with split-screen... right?).
- CommonUI opt-in and autocapture.
- Allowing Feature Flags to toggle [Gameplay Features](https://dev.epicgames.com/documentation/unreal-engine/game-features-and-modular-gameplay-in-unreal-engine).
- An IAnalyticsProvider wrapper so users using that have a drop-in solution.

The intention is that all of this is currently deferred until the inclusion of Feature Flags and Session Replay.

## The screenshots for blueprints look weird and are spaghetti.

That's not a question.

My goal with those screenshots was to contain the entire piece of logic in a single screenshot, and with vanilla Blueprint behavior, ensuring some of the longer blocks of code did that leads to some less-than-favorable results.

The images were also converted into WebP format and compressed to optimize them for web. This was more important to me than the background of the Blueprint graph being preserved.

## Does pineapple belong on pizza?

No. I could make an exception if it was churrasco-style pineapple, though. But that's because of how good churrasco-style pineapple is, and not because it inherently belongs on pizza.
