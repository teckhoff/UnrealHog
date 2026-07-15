// Trevor Eckhoff, 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Events/PostHogEvent.h"

namespace PostHogEventRehydration
{
	struct FResult
	{
		TOptional<FPostHogEvent> Event;
		FString Diagnostic;

		bool IsSuccess() const;
	};

	FResult TryParsePersistedEventJson(const FString& EventJson);
}
