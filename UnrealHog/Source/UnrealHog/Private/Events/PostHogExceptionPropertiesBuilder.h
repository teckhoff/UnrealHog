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
	// have already been validated as non-blank by the caller. PersonUrl is precomputed by the
	// caller from the effective current distinct id (empty when no effective distinct id exists
	// yet, e.g. pre-consent); when non-empty it is attached as the SDK-owned $exception_personURL,
	// appended after caller-supplied properties so it always wins on key collision.
	void Build(UPostHogEventProperties& Props, const FPostHogExceptionInput& Exception, const FString& PersonUrl);
}
