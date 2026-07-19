#include "Http/PostHogEndpointUrls.h"

namespace PostHogEndpointUrls
{
	FString NormalizeHost(const FString& InHost)
	{
		FString NormalizedHost = InHost.TrimStartAndEnd();
		while (NormalizedHost.EndsWith(TEXT("/")))
		{
			NormalizedHost.LeftChopInline(1);
		}

		return NormalizedHost;
	}

	FString BuildBatchUrl(const FString& Host)
	{
		const FString CanonicalHost = NormalizeHost(Host);
		return FString::Printf(TEXT("%s/batch"), *CanonicalHost);
	}

	FString BuildFeatureFlagsUrl(const FString& Host)
	{
		const FString CanonicalHost = NormalizeHost(Host);
		return FString::Printf(TEXT("%s/flags/?v=2"), *CanonicalHost);
	}

	FString BuildSessionReplayUrl(const FString& Host)
	{
		const FString CanonicalHost = NormalizeHost(Host);
		return FString::Printf(TEXT("%s/s/"), *CanonicalHost);
	}
}
