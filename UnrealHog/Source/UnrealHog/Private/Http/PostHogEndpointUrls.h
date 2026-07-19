#pragma once

#include "CoreMinimal.h"

namespace PostHogEndpointUrls
{
	FString NormalizeHost(const FString& InHost);
	FString BuildBatchUrl(const FString& Host);
	FString BuildFeatureFlagsUrl(const FString& Host);
	FString BuildSessionReplayUrl(const FString& Host);

	// Builds the PostHog person-detail URL for an exception event: <person-host>/project/<api-key>/person/<distinct-id>.
	// The person host is the canonical ingest host with ".i." replaced by "." (matching the Unity SDK).
	// URL-path encoding policy: ApiKey and DistinctId are percent-encoded independently via
	// FGenericPlatformHttp::UrlEncode, so a literal '/' or non-ASCII character in either value can
	// never introduce, remove, or merge a path segment.
	FString BuildPersonUrl(const FString& IngestHost, const FString& ApiKey, const FString& DistinctId);
}
