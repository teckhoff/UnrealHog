// Trevor Eckhoff, 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"

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
};

namespace PostHogSettingsValidation
{
	// Validates a settings snapshot without throwing and without creating any runtime collaborators.
	FPostHogSettingsValidationResult Validate(const UPostHogDeveloperSettings& Settings);
}
