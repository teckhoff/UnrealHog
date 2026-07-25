#pragma once

#include "CoreMinimal.h"
#include "Logging/LogVerbosity.h"
#include "PostHogLogger.generated.h"

// Compile-time verbosity is kept at `All` so Debug-level diagnostics remain compiled in and can be
// enabled in supported targets via the project setting or a runtime `Log LogUnrealHog` console command.
DECLARE_LOG_CATEGORY_EXTERN(LogUnrealHog, Log, All);

UENUM(BlueprintType)
enum class EPostHogLogLevel : uint8
{
	Debug,
	Info,
	Warning,
	Error,
	None
};

// Focused internal controller that translates the configured SDK log level to the runtime verbosity
// threshold of the process-global `LogUnrealHog` category. The mapping is intentionally explicit and
// does not rely on enum ordinal equivalence between EPostHogLogLevel and ELogVerbosity.
class PostHogLogger
{
public:
	PostHogLogger() = delete;

	// Maps the public SDK log level onto the Unreal verbosity that becomes the category threshold.
	// Debug->VeryVerbose, Info->Log, Warning->Warning, Error->Error, None->NoLogging.
	static ELogVerbosity::Type ToVerbosity(EPostHogLogLevel Level);

	// Applies the configured level as the startup runtime verbosity of `LogUnrealHog`. Standard Unreal
	// console commands may temporarily override the category afterwards; a later call reapplies the
	// project setting deterministically.
	static void ApplyConfiguredLevel(EPostHogLogLevel Level);
};
