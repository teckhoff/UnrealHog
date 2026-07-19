#include "PostHogSettingsValidation.h"

#include "Http/PostHogEndpointUrls.h"
#include "PostHogDeveloperSettings.h"

namespace
{
	constexpr const TCHAR* FallbackHost = TEXT("https://us.i.posthog.com");
}

FPostHogSettingsValidationResult PostHogSettingsValidation::Validate(const UPostHogDeveloperSettings& Settings)
{
	FPostHogSettingsValidationResult Result;

	if (Settings.GetApiKey().TrimStartAndEnd().IsEmpty())
	{
		Result.bIsValid = false;
		Result.FailureReason = TEXT("API key is missing or whitespace.");
		return Result;
	}

	if (Settings.GetFlushEventCount() < 1)
	{
		Result.bIsValid = false;
		Result.FailureReason = TEXT("Flush event count must be at least 1.");
		return Result;
	}

	if (Settings.GetFlushIntervalSeconds() < 1)
	{
		Result.bIsValid = false;
		Result.FailureReason = TEXT("Flush interval seconds must be at least 1.");
		return Result;
	}

	if (Settings.GetMaxQueueSize() < 1)
	{
		Result.bIsValid = false;
		Result.FailureReason = TEXT("Max queue size must be at least 1.");
		return Result;
	}

	if (Settings.GetMaxBatchSize() < 1)
	{
		Result.bIsValid = false;
		Result.FailureReason = TEXT("Max batch size must be at least 1.");
		return Result;
	}

	FString ResolvedHost = PostHogEndpointUrls::NormalizeHost(Settings.GetResolvedHost());

	if (ResolvedHost.IsEmpty())
	{
		ResolvedHost = FallbackHost;
	}

	Result.bIsValid = true;
	Result.ResolvedHost = ResolvedHost;
	Result.PersonProfiles = Settings.GetPersonProfiles();

	return Result;
}
