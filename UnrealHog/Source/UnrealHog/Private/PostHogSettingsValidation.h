#pragma once

#include "CoreMinimal.h"
#include "PostHogDeveloperSettings.h"
#include "SessionReplay/PostHogSessionReplayConfigValidation.h"

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

	// True when session replay is enabled but its serialized configuration is out of range. Replay
	// stays off and reports its own actionable diagnostic instead of the generic SDKP-018 notice;
	// core analytics is unaffected, so bIsValid can still be true.
	bool bSessionReplayConfigInvalid = false;
	FString SessionReplayConfigFailureReason;

	// Only set when replay is enabled and its configuration passed runtime validation. Absent
	// configuration is the signal that no replay collaborator may be constructed.
	bool bHasValidatedSessionReplayConfig = false;
	FPostHogValidatedSessionReplayConfig ValidatedSessionReplayConfig;

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
