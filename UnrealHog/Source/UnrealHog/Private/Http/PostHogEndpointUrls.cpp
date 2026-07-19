#include "Http/PostHogEndpointUrls.h"

#include "GenericPlatform/GenericPlatformHttp.h"

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

	FString BuildPersonUrl(const FString& IngestHost, const FString& ApiKey, const FString& DistinctId)
	{
		const FString PersonHost = NormalizeHost(IngestHost).Replace(TEXT(".i."), TEXT("."));
		const FString EncodedApiKey = FGenericPlatformHttp::UrlEncode(ApiKey);
		const FString EncodedDistinctId = FGenericPlatformHttp::UrlEncode(DistinctId);
		return FString::Printf(TEXT("%s/project/%s/person/%s"), *PersonHost, *EncodedApiKey, *EncodedDistinctId);
	}
}
