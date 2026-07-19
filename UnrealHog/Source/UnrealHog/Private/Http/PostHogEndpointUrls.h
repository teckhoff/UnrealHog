#pragma once

#include "CoreMinimal.h"

namespace PostHogEndpointUrls
{
	FString NormalizeHost(const FString& InHost);
	FString BuildBatchUrl(const FString& Host);
	FString BuildFeatureFlagsUrl(const FString& Host);
	FString BuildSessionReplayUrl(const FString& Host);
}
