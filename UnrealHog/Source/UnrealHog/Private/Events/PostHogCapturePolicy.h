#pragma once

#include "CoreMinimal.h"

enum class EPostHogPersonProfiles : uint8;

/**
 * @brief Single source of truth for capture-composition policy: event name validity, the
 * SDK-owned property keys that caller input must never override, and the person-profile
 * processing decision derived from EPostHogPersonProfiles and current identity state.
 *
 * Shared by UPostHogEventProperties::ApplyToEvent (strips reserved keys from caller input) and
 * FPostHogConsentController::CaptureEvent (rejects invalid event names, computes
 * $process_person_profile) so the rules cannot drift.
 */
namespace PostHogCapturePolicy
{
	// False for an empty or whitespace-only event name.
	bool IsValidEventName(const FString& EventName);

	// The SDK-owned property keys that caller-supplied properties may never set directly.
	const TSet<FString>& GetReservedPropertyKeys();

	// The policy/identity matrix for person-profile processing: Never is always false;
	// IdentifiedOnly matches the current identity state; Always (and any future default) is true.
	bool ShouldProcessPersonProfile(EPostHogPersonProfiles Policy, bool bIsIdentified);
}
