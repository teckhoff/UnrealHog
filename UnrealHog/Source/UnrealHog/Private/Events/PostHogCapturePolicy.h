#pragma once

#include "CoreMinimal.h"
#include "Containers/Set.h"

/**
 * @brief Single source of truth for capture-composition policy: event name validity and the
 * SDK-owned property keys that caller input must never override.
 *
 * Shared by UPostHogEventProperties::ApplyToEvent (strips reserved keys from caller input) and
 * FPostHogConsentController::CaptureEvent (rejects invalid event names) so the rules cannot drift.
 */
namespace PostHogCapturePolicy
{
	// False for an empty or whitespace-only event name.
	bool IsValidEventName(const FString& EventName);

	// The SDK-owned property keys that caller-supplied properties may never set directly.
	const TSet<FString>& GetReservedPropertyKeys();
}
