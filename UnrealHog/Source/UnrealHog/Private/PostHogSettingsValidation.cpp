#include "PostHogSettingsValidation.h"

#include "Http/PostHogEndpointUrls.h"
#include "Logging/PostHogLogger.h"
#include "PostHogDeveloperSettings.h"

namespace
{
	constexpr const TCHAR* FallbackHost = TEXT("https://us.i.posthog.com");
	constexpr const TCHAR* FeatureFlagPreloadUnavailableDiagnostic = TEXT("PostHog feature-flag preload is unavailable until SDKP-012; bPreloadFeatureFlags remains serialized but no feature-flag preload request will be made.");
	constexpr const TCHAR* SessionReplayUnavailableDiagnostic = TEXT("PostHog session replay is unavailable until SDKP-018; bSessionReplay remains serialized but no session replay capture will start.");

	bool bLoggedFeatureFlagPreloadUnavailable = false;
	bool bLoggedSessionReplayUnavailable = false;

	void PopulateUnavailableCapabilityDiagnostics(const UPostHogDeveloperSettings& Settings, FPostHogSettingsValidationResult& Result)
	{
		if (Settings.ShouldPreloadFeatureFlags())
		{
			Result.bFeatureFlagPreloadUnavailable = true;
			Result.UnavailableCapabilityDiagnostics.Add(FeatureFlagPreloadUnavailableDiagnostic);
		}

		if (Settings.IsSessionReplayEnabled())
		{
			Result.bSessionReplayUnavailable = true;
			Result.UnavailableCapabilityDiagnostics.Add(SessionReplayUnavailableDiagnostic);
		}
	}
}

FPostHogSettingsValidationResult PostHogSettingsValidation::Validate(const UPostHogDeveloperSettings& Settings)
{
	FPostHogSettingsValidationResult Result;
	PopulateUnavailableCapabilityDiagnostics(Settings, Result);

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

void PostHogSettingsValidation::LogUnavailableCapabilityDiagnosticsOnce(const FPostHogSettingsValidationResult& Result)
{
	if (Result.bFeatureFlagPreloadUnavailable && !bLoggedFeatureFlagPreloadUnavailable)
	{
		UE_LOG(LogUnrealHog, Warning, TEXT("%s"), FeatureFlagPreloadUnavailableDiagnostic);
		bLoggedFeatureFlagPreloadUnavailable = true;
	}

	if (Result.bSessionReplayUnavailable && !bLoggedSessionReplayUnavailable)
	{
		UE_LOG(LogUnrealHog, Warning, TEXT("%s"), SessionReplayUnavailableDiagnostic);
		bLoggedSessionReplayUnavailable = true;
	}
}

#if WITH_DEV_AUTOMATION_TESTS
void PostHogSettingsValidation::ResetUnavailableCapabilityDiagnosticLogStateForTests()
{
	bLoggedFeatureFlagPreloadUnavailable = false;
	bLoggedSessionReplayUnavailable = false;
}
#endif
