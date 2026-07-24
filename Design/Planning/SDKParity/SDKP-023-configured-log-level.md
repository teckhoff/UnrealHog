# SDKP-023: Apply the configured SDK logging level

## Status and dependencies

- **State:** Completed
- **Blocked by:** None
- **Blocks:** Honest runtime diagnostics and supportability for every SDK feature family
- **Parity row:** Configured minimum severity for SDK-owned diagnostic logging

## Goal

Make `UPostHogDeveloperSettings::LogLevel` control UnrealHog's own diagnostic output with the same
observable severity threshold as posthog-unity, implemented through idiomatic Unreal log-category
filtering and a severity audit of every `LogUnrealHog` call site.

## Behavior

### Threshold mapping

`PostHogLogger::ToVerbosity` translates the public `EPostHogLogLevel` to the runtime verbosity
threshold applied to the process-global `LogUnrealHog` category. The mapping is an explicit switch and
does not rely on enum ordinal equivalence:

| `EPostHogLogLevel` | `ELogVerbosity` threshold | Admitted UnrealHog output |
| --- | --- | --- |
| `Debug` | `VeryVerbose` | Error, Warning, Log, Verbose, VeryVerbose |
| `Info` | `Log` | Error, Warning, Log |
| `Warning` (default) | `Warning` | Error, Warning |
| `Error` | `Error` | Error |
| `None` | `NoLogging` | (none) |

The category is declared `DECLARE_LOG_CATEGORY_EXTERN(LogUnrealHog, Log, All)`: compile-time verbosity
stays at `All` so Debug diagnostics remain compiled into supported targets and can be enabled.

### Startup precedence and runtime override

`PostHogLogger::ApplyConfiguredLevel(Settings->LogLevel)` runs in `UPostHogRuntimeSubsystem::Initialize`
immediately after reading `UPostHogDeveloperSettings` and before any collaborator (consent restoration,
settings validation, storage, lifecycle, exception capture, or transport) can emit a diagnostic.

The project setting is the startup verbosity for the process. A standard Unreal console command such as
`Log LogUnrealHog VeryVerbose` may temporarily raise or lower the category after initialization for
live diagnostics; because every subsystem initialization reapplies the project setting, a later
initialization deterministically restores the configured level.

Because `LogLevel` is project-wide and `LogUnrealHog` is process-global, a single category threshold is
correct even when multiple game instances share the same project settings.

### Severity audit

Existing call sites were reclassified by operational meaning rather than only calling `SetVerbosity`:

- **Consent-gated no-ops → Verbose.** Capturing, identifying, grouping, registering/clearing super
  properties, or flushing without consent is normal privacy behavior and no longer produces warnings at
  the default level. The raw distinct id was removed from the `Identify` no-op diagnostic.
- **Retryable batch delivery failure + backoff → Verbose.** Permanent rejection, HTTP 413 limit
  reduction, and delete/persistence failures remain at Warning; enqueue failures remain at Error.
- **High-volume storage path detail → VeryVerbose** (`Using base path`).
- **Capacity eviction of the oldest persisted event → Verbose.**

### Focused added diagnostics (non-sensitive)

- Initialization with/without consent (Log).
- Consent opt-in granted / opt-out revoked transitions (Log, enum outcome only).
- Capture acceptance/enqueue (Verbose, event name only), rejection, and before-send failure.
- Automatic exception-capture handler registration vs. inactive state (Log).

No token, authorization data, complete payload, event property value, exception body/stack, response
body, or raw distinct/group identifier is logged. Structured `UE_LOGFMT` call sites keep formatting work
behind the category threshold.

### Distinction from session replay

`LogLevel` controls the SDK's operational log output only. It is separate from the session-replay
console-log capture threshold (`FPostHogSessionReplayConfig::MinLogLevel`, owned by SDKP-016), which
decides which game log lines are recorded into a replay.

## Tests

`UnrealHog/Source/UnrealHog/Private/Tests/PostHogLoggerTests.cpp`:

- `UnrealHog.Logging.Logger.MapsLevelsToVerbosity` asserts the explicit mapping for all five levels.
- `UnrealHog.Logging.Logger.CategoryThresholdFiltersBySeverity` applies every configured level and, via
  the category threshold seam (`FLogCategoryBase::IsSuppressed`, the same check the `UE_LOG` macros
  consult before any formatting), asserts exactly the admitted severities per level (Warning admits
  Error+Warning; Debug admits all five; Info admits Log+; Error admits only Error; None admits none). An
  RAII guard snapshots and restores the global `LogUnrealHog` verbosity between cases so it cannot
  contaminate other tests, and it contacts no network.

## Unity references

- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/Utilities/PostHogLogger.cs`
- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/PostHogConfig.cs`
- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/PostHogSDK.cs`
