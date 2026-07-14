# UnrealHog Plugin Guidelines

These rules apply to `Plugins/UnrealHog` in addition to the repository-level `AGENTS.md`.

## Source Layout and API Boundaries

- Put exported SDK headers under `Source/UnrealHog/Public` and implementation-only headers and sources under `Source/UnrealHog/Private`.
- Keep HTTP, persistence, queueing, serialization, and policy details private unless consumers need a stable C++ type.
- Organize related code into focused subdirectories such as `Events`, `Http`, `Storage`, and `Subsystems`; mirror public and private paths where a type has both an exported declaration and implementation.
- Plugin resources belong in `Resources`. Do not edit or commit generated `Binaries` or `Intermediate` output as source work.

## Unreal and Blueprint APIs

- Public runtime orchestration belongs on `UPostHogRuntimeSubsystem`, a `UGameInstanceSubsystem`; keep payload, queue, storage, and transport behavior in testable supporting types.
- Use clear Blueprint-callable names such as `CaptureEvent`, `Identify`, `Flush`, and `SetAnalyticsOptIn`. Expose enums and structured property builders instead of ambiguous Boolean switches when states have distinct semantics.
- Keep Blueprint metadata, categories, display names, and success/failure behavior consistent across related APIs. Validate input at the public boundary and make failures observable without crashing the game.
- SDK-owned event properties take precedence over caller-provided values. Centralize reserved-property policy rather than scattering string checks.

## Module Dependencies

- Declare a module in `UnrealHog.Build.cs` only when plugin code directly depends on it.
- Prefer `PrivateDependencyModuleNames`; use `PublicDependencyModuleNames` only when an exported header exposes types from that module.
- Expected dependencies include `HTTP`, `Json`, and `DeveloperSettings` when their APIs are used. Avoid adding editor-only dependencies to the runtime module; guard editor-only behavior with the appropriate build macros or place it in an editor module.

## Tests

- Put Unreal Automation tests in the plugin module under `Source/UnrealHog/Private/Tests`, grouped by behavior or parity row.
- Test payloads, consent gates, URL resolution, session/identity transitions, durable ordering, and retry classification without live credentials.
- Prefer dependency seams for clocks, GUID generation, storage, and HTTP so tests are deterministic and do not require an Unreal Editor network connection.

