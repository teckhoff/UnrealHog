#pragma once

#include "CoreMinimal.h"
#include "PostHogDeveloperSettings.h"

class UPostHogDeveloperSettings;

/**
 * @brief Side-effect-free validation of a PostHog developer settings snapshot.
 *
 * Shared by both automation tests and runtime subsystem gating so a single set of rules
 * decides whether it is safe to construct SDK collaborators (UUIDs, storage, HTTP, queue).
 */
struct FPostHogSettingsValidationResult
{
	bool bIsValid = false;
	FString ResolvedHost;
	FString FailureReason;
	EPostHogPersonProfiles PersonProfiles = EPostHogPersonProfiles::IdentifiedOnly;
	bool bFeatureFlagPreloadUnavailable = false;
	bool bSessionReplayUnavailable = false;
	TArray<FString> UnavailableCapabilityDiagnostics;
};

namespace PostHogSettingsValidation
{
	// Validates a settings snapshot without throwing and without creating any runtime collaborators.
	FPostHogSettingsValidationResult Validate(const UPostHogDeveloperSettings& Settings);

	// Emits at most one process-wide warning per unavailable settings family.
	void LogUnavailableCapabilityDiagnosticsOnce(const FPostHogSettingsValidationResult& Result);

#if WITH_DEV_AUTOMATION_TESTS
	void ResetUnavailableCapabilityDiagnosticLogStateForTests();
#endif
}
