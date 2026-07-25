# UnrealHog

> ## UnrealHog has no affiliation with PostHog
>
> UnrealHog is an independent, unofficial community project. It is not created, maintained, endorsed, or supported by PostHog, Inc. “PostHog” is used only to describe compatibility with the PostHog service.

UnrealHog is an Unreal Engine 5.8 C++ plugin for sending consent-gated product analytics and exception events to PostHog. It brings the observable behavior of PostHog's Unity SDK to Unreal through idiomatic engine systems: project-wide `UDeveloperSettings`, a `UGameInstanceSubsystem`, and APIs designed for both C++ and Blueprints.

Analytics collection is opt-in by default. Until collection is permitted, UnrealHog does not create an event payload, queue record, persistence file, or HTTP request.

## Development Approach

UnrealHog was developed using [Zeroshot](https://github.com/the-open-engine/zeroshot) and agentic development loops instead of manual coding. Scoped tasks, behavioral parity requirements, acceptance criteria, automated implementation, review, and validation loops guided the work. The Unity SDK is treated as a behavioral reference; the Unreal implementation is designed around Unreal Engine conventions rather than as a mechanical translation.


## Documentation

<!-- PUBLISHED_DOCUMENTATION_URL: Replace this comment with a link to the published documentation after the GitHub Pages action completes successfully. -->

**Published documentation:** _Link will be added after the GitHub Pages deployment succeeds._

The documentation source is available in [`docs/`](docs/index.md).

## Implemented Features

- **C++ and Blueprint APIs** — Access analytics through the game-instance runtime subsystem, with event-property helpers for strings, numbers, booleans, nulls, nested objects, and arrays.
- **Consent-first collection** — Read, apply, and persist a player's analytics choice. Opting out cancels active delivery and clears queued events; capture and state-changing calls are ignored while collection is not permitted.
- **Product analytics** — Capture custom events and `$screen` events with per-event properties plus automatically composed SDK, application, platform, device, viewport, session, and group context when available.
- **Identity and person profiles** — Start with an anonymous distinct ID, identify authenticated players, create aliases, configure person-profile processing, and reset identity safely for sign-out or account switching.
- **Super properties** — Register persistent properties that are added to future events, then replace, unregister, or clear them as game context changes.
- **Group analytics** — Associate players and future events with organizations, guilds, teams, or other groups, and optionally send group properties. Group analytics may require a paid PostHog add-on.
- **Application lifecycle events** — Automatically capture consented install, update, open, and background events, including persisted version history where available.
- **Durable asynchronous delivery** — Persist accepted events in a bounded file-backed queue and send them in configurable batches. Delivery supports count- and timer-based flushing, background and shutdown flushes, offline retention, retry backoff, queue-capacity handling, and response-aware failure behavior.
- **Error tracking with explicit limits** — Automatically capture Unreal `ensure` failures and manually report handled failures with optional stack information. UnrealHog does not replace a dedicated fatal crash-reporting or minidump solution.
- **Before-send control for C++** — Inspect and modify the final event properties immediately before persistence, or deliberately drop an event.
- **Configurable ingestion and diagnostics** — Select PostHog's US or EU ingestion host, supply a custom host, tune queue and flush behavior, control lifecycle and exception capture, and configure UnrealHog logging from Project Settings.

Feature flags and session replay are not implemented. Their settings are currently serialized only as forward-compatible placeholders and have no runtime effect.

## Engine and Platform Support

UnrealHog is developed and tested for Unreal Engine 5.8. Prepackaged releases currently target Windows x64 and Linux; other targets can use the plugin from source or package it for a compatible engine and platform toolchain.

See the [installation guide](docs/installation.md) and [quickstart](docs/quickstart.md) to add the plugin, configure a development PostHog project, grant test consent, and verify a first event.