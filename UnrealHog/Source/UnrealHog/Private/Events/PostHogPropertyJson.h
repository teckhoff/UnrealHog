#pragma once

#include "CoreMinimal.h"

struct FPostHogEventProperty;
class FJsonValue;

/**
 * @brief Shared FPostHogEventProperty <-> FJsonValue conversion, used by both call-supplied event
 * properties (UPostHogEventProperties::ApplyToEvent) and persisted super properties
 * (FPostHogSuperPropertiesManager) so the two representations cannot drift.
 */
namespace PostHogPropertyJson
{
	TSharedRef<FJsonValue> ToJsonValue(const FPostHogEventProperty& Property);

	// Reconstructs a property from a parsed JSON value. Key is assigned to the returned property's
	// Key field; for Array elements (whose Key is not semantically meaningful) callers pass an
	// empty FString. Object fields with an empty name and Array elements are otherwise handled
	// identically via recursion into Children.
	FPostHogEventProperty FromJsonValue(const FString& Key, const FJsonValue& Value);
}
