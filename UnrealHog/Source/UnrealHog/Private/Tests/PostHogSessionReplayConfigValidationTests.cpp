#include "SessionReplay/PostHogSessionReplayConfigValidation.h"

#if WITH_DEV_AUTOMATION_TESTS

#include <limits>

#include "Misc/AutomationTest.h"
#include "PostHogDeveloperSettings.h"
#include "PostHogSettingsValidation.h"
#include "Tests/PostHogTestPropertyHelpers.h"
#include "UObject/Package.h"

namespace PostHogReplayConfigTests
{
	FPostHogSessionReplayConfig MakeDefaultConfig()
	{
		return FPostHogSessionReplayConfig();
	}

	bool Validate(const FPostHogSessionReplayConfig& Config, FString& OutFailureReason)
	{
		FPostHogValidatedSessionReplayConfig Validated;
		return PostHogSessionReplayConfigValidation::TryValidate(Config, Validated, OutFailureReason);
	}

	UPostHogDeveloperSettings* MakeReplaySettings(const FPostHogSessionReplayConfig& Config, bool bReplayEnabled)
	{
		UPostHogDeveloperSettings* Settings = NewObject<UPostHogDeveloperSettings>(GetTransientPackage());
		UnrealHogTests::SetPropertyValue<FString>(Settings, TEXT("ApiKey"), TEXT("phc_replay_test"));
		// Isolate replay diagnostics from the unrelated SDKP-012 feature-flag preload notice.
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bPreloadFeatureFlags"), false);
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bSessionReplay"), bReplayEnabled);
		UnrealHogTests::SetPropertyValue<FPostHogSessionReplayConfig>(Settings, TEXT("SessionReplayConfig"), Config);
		return Settings;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogSessionReplayConfigRuntimeBoundsTest, "UnrealHog.SessionReplay.Configuration.RuntimeBounds", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogSessionReplayConfigRuntimeBoundsTest::RunTest(const FString& Parameters)
{
	using namespace PostHogReplayConfigTests;

	FString FailureReason;

	TestTrue(TEXT("Serialized defaults validate"), Validate(MakeDefaultConfig(), FailureReason));

	// Throttle delay: at least 0.1 seconds, finite.
	{
		FPostHogSessionReplayConfig Config = MakeDefaultConfig();
		Config.ThrottleDelaySeconds = 0.1f;
		TestTrue(TEXT("Throttle at lower bound is accepted"), Validate(Config, FailureReason));

		Config.ThrottleDelaySeconds = 0.09f;
		TestFalse(TEXT("Throttle below lower bound is rejected"), Validate(Config, FailureReason));
		TestTrue(TEXT("Throttle rejection names the field"), FailureReason.Contains(TEXT("ThrottleDelaySeconds")));

		Config.ThrottleDelaySeconds = 0.0f;
		TestFalse(TEXT("Zero throttle is rejected"), Validate(Config, FailureReason));

		Config.ThrottleDelaySeconds = -1.0f;
		TestFalse(TEXT("Negative throttle is rejected"), Validate(Config, FailureReason));

		Config.ThrottleDelaySeconds = std::numeric_limits<float>::quiet_NaN();
		TestFalse(TEXT("NaN throttle is rejected"), Validate(Config, FailureReason));

		Config.ThrottleDelaySeconds = std::numeric_limits<float>::infinity();
		TestFalse(TEXT("Infinite throttle is rejected"), Validate(Config, FailureReason));
	}

	// Screenshot quality: 1-100 inclusive.
	{
		FPostHogSessionReplayConfig Config = MakeDefaultConfig();
		Config.ScreenshotQuality = 1;
		TestTrue(TEXT("Quality at lower bound is accepted"), Validate(Config, FailureReason));

		Config.ScreenshotQuality = 100;
		TestTrue(TEXT("Quality at upper bound is accepted"), Validate(Config, FailureReason));

		Config.ScreenshotQuality = 0;
		TestFalse(TEXT("Quality below lower bound is rejected"), Validate(Config, FailureReason));
		TestTrue(TEXT("Quality rejection names the field"), FailureReason.Contains(TEXT("ScreenshotQuality")));

		Config.ScreenshotQuality = 101;
		TestFalse(TEXT("Quality above upper bound is rejected"), Validate(Config, FailureReason));

		Config.ScreenshotQuality = -5;
		TestFalse(TEXT("Negative quality is rejected"), Validate(Config, FailureReason));
	}

	// Screenshot scale: 0.1-1.0 inclusive, finite.
	{
		FPostHogSessionReplayConfig Config = MakeDefaultConfig();
		Config.ScreenshotScale = 0.1f;
		TestTrue(TEXT("Scale at lower bound is accepted"), Validate(Config, FailureReason));

		Config.ScreenshotScale = 1.0f;
		TestTrue(TEXT("Scale at upper bound is accepted"), Validate(Config, FailureReason));

		Config.ScreenshotScale = 0.09f;
		TestFalse(TEXT("Scale below lower bound is rejected"), Validate(Config, FailureReason));
		TestTrue(TEXT("Scale rejection names the field"), FailureReason.Contains(TEXT("ScreenshotScale")));

		Config.ScreenshotScale = 1.01f;
		TestFalse(TEXT("Scale above upper bound is rejected"), Validate(Config, FailureReason));

		Config.ScreenshotScale = std::numeric_limits<float>::quiet_NaN();
		TestFalse(TEXT("NaN scale is rejected"), Validate(Config, FailureReason));

		Config.ScreenshotScale = std::numeric_limits<float>::infinity();
		TestFalse(TEXT("Infinite scale is rejected"), Validate(Config, FailureReason));
	}

	// Log level: only the three declared enumerators. Config files and reflection can deliver others.
	{
		FPostHogSessionReplayConfig Config = MakeDefaultConfig();
		Config.MinLogLevel = EPostHogSessionReplayLogLevel::Log;
		TestTrue(TEXT("Log level Log is accepted"), Validate(Config, FailureReason));

		Config.MinLogLevel = EPostHogSessionReplayLogLevel::Warning;
		TestTrue(TEXT("Log level Warning is accepted"), Validate(Config, FailureReason));

		Config.MinLogLevel = EPostHogSessionReplayLogLevel::Error;
		TestTrue(TEXT("Log level Error is accepted"), Validate(Config, FailureReason));

		Config.MinLogLevel = static_cast<EPostHogSessionReplayLogLevel>(37);
		TestFalse(TEXT("Out-of-range log level is rejected"), Validate(Config, FailureReason));
		TestTrue(TEXT("Log level rejection names the field"), FailureReason.Contains(TEXT("MinLogLevel")));
	}

	// Queue and flush values: at least 1.
	{
		FPostHogSessionReplayConfig Config = MakeDefaultConfig();
		Config.FlushEventCount = 1;
		Config.FlushIntervalSeconds = 1;
		Config.MaxQueueSize = 1;
		TestTrue(TEXT("Queue and flush lower bounds are accepted"), Validate(Config, FailureReason));

		Config = MakeDefaultConfig();
		Config.FlushEventCount = 0;
		TestFalse(TEXT("Zero flush event count is rejected"), Validate(Config, FailureReason));
		TestTrue(TEXT("Flush count rejection names the field"), FailureReason.Contains(TEXT("FlushEventCount")));

		Config = MakeDefaultConfig();
		Config.FlushIntervalSeconds = 0;
		TestFalse(TEXT("Zero flush interval is rejected"), Validate(Config, FailureReason));
		TestTrue(TEXT("Flush interval rejection names the field"), FailureReason.Contains(TEXT("FlushIntervalSeconds")));

		Config = MakeDefaultConfig();
		Config.MaxQueueSize = 0;
		TestFalse(TEXT("Zero max queue size is rejected"), Validate(Config, FailureReason));
		TestTrue(TEXT("Queue size rejection names the field"), FailureReason.Contains(TEXT("MaxQueueSize")));

		Config = MakeDefaultConfig();
		Config.MaxQueueSize = -10;
		TestFalse(TEXT("Negative max queue size is rejected"), Validate(Config, FailureReason));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogSessionReplayConfigCopiesValidatedFieldsTest, "UnrealHog.SessionReplay.Configuration.ValidatedFieldsAreCopied", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogSessionReplayConfigCopiesValidatedFieldsTest::RunTest(const FString& Parameters)
{
	FPostHogSessionReplayConfig Config;
	Config.ThrottleDelaySeconds = 0.25f;
	Config.ScreenshotQuality = 42;
	Config.ScreenshotScale = 0.5f;
	Config.bCaptureNetworkTelemetry = false;
	Config.bCaptureLogs = true;
	Config.MinLogLevel = EPostHogSessionReplayLogLevel::Warning;
	Config.FlushEventCount = 7;
	Config.FlushIntervalSeconds = 11;
	Config.MaxQueueSize = 13;

	FPostHogValidatedSessionReplayConfig Validated;
	FString FailureReason;

	if (!TestTrue(TEXT("Configuration validates"), PostHogSessionReplayConfigValidation::TryValidate(Config, Validated, FailureReason)))
	{
		return false;
	}

	TestTrue(TEXT("Success clears the failure reason"), FailureReason.IsEmpty());
	TestEqual(TEXT("Throttle copied"), Validated.ThrottleDelaySeconds, 0.25f);
	TestEqual(TEXT("Quality copied"), Validated.ScreenshotQuality, 42);
	TestEqual(TEXT("Scale copied"), Validated.ScreenshotScale, 0.5f);
	TestFalse(TEXT("Network telemetry flag copied"), Validated.bCaptureNetworkTelemetry);
	TestTrue(TEXT("Log capture flag copied"), Validated.bCaptureLogs);
	TestTrue(TEXT("Min log level copied"), Validated.MinLogLevel == EPostHogSessionReplayLogLevel::Warning);
	TestEqual(TEXT("Flush event count copied"), Validated.FlushEventCount, 7);
	TestEqual(TEXT("Flush interval copied"), Validated.FlushIntervalSeconds, 11);
	TestEqual(TEXT("Max queue size copied"), Validated.MaxQueueSize, 13);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogSessionReplayInvalidReplayIsolatedTest, "UnrealHog.SessionReplay.Configuration.InvalidReplayIsolated", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogSessionReplayInvalidReplayIsolatedTest::RunTest(const FString& Parameters)
{
	using namespace PostHogReplayConfigTests;

	// Replay disabled: nothing is validated and no replay-ready configuration is exposed.
	{
		FPostHogSessionReplayConfig Config;
		Config.MaxQueueSize = 0;

		const FPostHogSettingsValidationResult Result = PostHogSettingsValidation::Validate(*MakeReplaySettings(Config, false));

		TestTrue(TEXT("Core analytics remains valid with replay off"), Result.bIsValid);
		TestFalse(TEXT("Disabled replay reports no configuration failure"), Result.bSessionReplayConfigInvalid);
		TestFalse(TEXT("Disabled replay exposes no validated configuration"), Result.bHasValidatedSessionReplayConfig);
		TestFalse(TEXT("Disabled replay emits no unavailable notice"), Result.bSessionReplayUnavailable);
		TestEqual(TEXT("Disabled replay adds no diagnostic"), Result.UnavailableCapabilityDiagnostics.Num(), 0);
	}

	// Replay enabled with a valid configuration: validated configuration is exposed alongside the
	// existing SDKP-018 unavailable notice.
	{
		const FPostHogSettingsValidationResult Result = PostHogSettingsValidation::Validate(*MakeReplaySettings(FPostHogSessionReplayConfig(), true));

		TestTrue(TEXT("Core analytics remains valid"), Result.bIsValid);
		TestTrue(TEXT("Valid replay exposes a validated configuration"), Result.bHasValidatedSessionReplayConfig);
		TestFalse(TEXT("Valid replay reports no configuration failure"), Result.bSessionReplayConfigInvalid);
		TestTrue(TEXT("Valid replay keeps the unavailable notice"), Result.bSessionReplayUnavailable);
		TestEqual(TEXT("Valid replay adds exactly one diagnostic"), Result.UnavailableCapabilityDiagnostics.Num(), 1);
		TestEqual(TEXT("Validated defaults are exposed"), Result.ValidatedSessionReplayConfig.MaxQueueSize, 100);
	}

	// Replay enabled with an invalid configuration: replay is disabled with one actionable
	// diagnostic that replaces the generic notice, and core analytics is untouched.
	{
		FPostHogSessionReplayConfig Config;
		Config.ScreenshotQuality = 250;

		const FPostHogSettingsValidationResult Result = PostHogSettingsValidation::Validate(*MakeReplaySettings(Config, true));

		TestTrue(TEXT("Core analytics stays valid despite invalid replay"), Result.bIsValid);
		TestTrue(TEXT("Core analytics reports no failure reason"), Result.FailureReason.IsEmpty());
		TestTrue(TEXT("Invalid replay is reported"), Result.bSessionReplayConfigInvalid);
		TestFalse(TEXT("Invalid replay exposes no validated configuration"), Result.bHasValidatedSessionReplayConfig);
		TestFalse(TEXT("Invalid replay suppresses the generic unavailable notice"), Result.bSessionReplayUnavailable);

		if (!TestEqual(TEXT("Invalid replay emits exactly one diagnostic"), Result.UnavailableCapabilityDiagnostics.Num(), 1))
		{
			return false;
		}

		const FString& Diagnostic = Result.UnavailableCapabilityDiagnostics[0];
		TestTrue(TEXT("Diagnostic states replay is disabled"), Diagnostic.Contains(TEXT("session replay is disabled")));
		TestTrue(TEXT("Diagnostic names the offending field"), Diagnostic.Contains(TEXT("ScreenshotQuality")));
		TestTrue(TEXT("Diagnostic tells the developer what to do"), Diagnostic.Contains(TEXT("Project Settings")));
		TestFalse(TEXT("Diagnostic is not the generic SDKP-018 notice"), Diagnostic.Contains(TEXT("unavailable until SDKP-018")));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
