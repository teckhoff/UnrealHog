#pragma once

#include "CoreMinimal.h"

class UPostHogEventProperties;
struct FPostHogExceptionInput;

/**
 * @brief Builds the PostHog `$exception` event payload ($exception_list nested structure and the
 * top-level $exception_* summary properties) from a caller-supplied FPostHogExceptionInput.
 *
 * Private: only FPostHogConsentController::CaptureException calls this. Mirrors
 * PostHogPropertyJson.h in scope (a stateless free function, not a class).
 */
namespace PostHogExceptionPropertiesBuilder
{
	// Appends the exception properties to Props. Assumes Exception.Message and Exception.Type
	// have already been validated as non-blank by the caller.
	void Build(UPostHogEventProperties& Props, const FPostHogExceptionInput& Exception);
}
