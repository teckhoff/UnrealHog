#include "PostHogDeveloperSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Logging/PostHogLogger.h"
#include "PostHogSettingsValidation.h"
#include "Tests/PostHogTestPropertyHelpers.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace
{
	UPostHogDeveloperSettings* MakeTransientSettings()
	{
		return NewObject<UPostHogDeveloperSettings>(GetTransientPackage());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogSettingsDefaultsTest, "UnrealHog.Configuration.DeveloperSettings.DefaultsAreCorrect", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogSettingsDefaultsTest::RunTest(const FString& Parameters)
{
	UPostHogDeveloperSettings* Settings = MakeTransientSettings();

	// Repository invariant: analytics collection is opt-in, diverging from the Unity default.
	// Analytics will still be enabled though. This flag is just for whether or not analytics as a whole are enabled
	// or disabled without removing the plugin.
	TestTrue(TEXT("Analytics enabled by default"), Settings->IsAnalyticsEnabled());

	// Repository invariant: the end user's default consent state is opt-out.
	TestFalse(TEXT("Default user opt-in is opt-out"), Settings->GetDefaultUserOptIn());

	TestEqual(TEXT("Default flush event count"), Settings->GetFlushEventCount(), 20);
	TestEqual(TEXT("Default flush interval seconds"), Settings->GetFlushIntervalSeconds(), 30);
	TestEqual(TEXT("Default max queue size"), Settings->GetMaxQueueSize(), 1000);
	TestEqual(TEXT("Default max batch size"), Settings->GetMaxBatchSize(), 50);
	TestTrue(TEXT("Default lifecycle capture enabled"), Settings->ShouldCaptureApplicationLifecycleEvents());

	TestTrue(TEXT("Default person profiles"), UnrealHogTests::GetPropertyValue<EPostHogPersonProfiles>(Settings, TEXT("PersonProfiles")) == EPostHogPersonProfiles::IdentifiedOnly);
	TestTrue(TEXT("Default log level"), UnrealHogTests::GetPropertyValue<EPostHogLogLevel>(Settings, TEXT("LogLevel")) == EPostHogLogLevel::Warning);
	TestFalse(TEXT("Default no anonymous ID reuse"), UnrealHogTests::GetPropertyValue<bool>(Settings, TEXT("bReuseAnonymousId")));
	TestTrue(TEXT("Default flush on quit enabled"), UnrealHogTests::GetPropertyValue<bool>(Settings, TEXT("bFlushOnQuit")));
	TestEqual(TEXT("Default flush on quit timeout"), UnrealHogTests::GetPropertyValue<float>(Settings, TEXT("FlushOnQuitTimeoutSeconds")), 3.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogSettingsHostResolutionTest, "UnrealHog.Configuration.DeveloperSettings.ResolvesUsAndEuHosts", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogSettingsHostResolutionTest::RunTest(const FString& Parameters)
{
	UPostHogDeveloperSettings* Settings = MakeTransientSettings();

	UnrealHogTests::SetPropertyValue<EPostHogHost>(Settings, TEXT("HostType"), EPostHogHost::US);
	TestEqual(TEXT("US resolves"), Settings->GetResolvedHost(), TEXT("https://us.i.posthog.com"));

	UnrealHogTests::SetPropertyValue<EPostHogHost>(Settings, TEXT("HostType"), EPostHogHost::EU);
	TestEqual(TEXT("EU resolves"), Settings->GetResolvedHost(), TEXT("https://eu.i.posthog.com"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogSettingsValidConfigTest, "UnrealHog.Configuration.DeveloperSettings.ValidConfigPasses", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogSettingsValidConfigTest::RunTest(const FString& Parameters)
{
	UPostHogDeveloperSettings* Settings = MakeTransientSettings();
	UnrealHogTests::SetPropertyValue<FString>(Settings, TEXT("ApiKey"), TEXT("phc_valid_key"));

	const FPostHogSettingsValidationResult Result = PostHogSettingsValidation::Validate(*Settings);
	TestTrue(TEXT("Valid config passes"), Result.bIsValid);
	TestEqual(TEXT("Resolved host defaults to US"), Result.ResolvedHost, TEXT("https://us.i.posthog.com"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogSettingsInvalidApiKeyTest, "UnrealHog.Configuration.DeveloperSettings.EmptyOrWhitespaceApiKeyFails", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogSettingsInvalidApiKeyTest::RunTest(const FString& Parameters)
{
	UPostHogDeveloperSettings* EmptyKeySettings = MakeTransientSettings();
	UnrealHogTests::SetPropertyValue<FString>(EmptyKeySettings, TEXT("ApiKey"), TEXT(""));
	TestFalse(TEXT("Empty API key fails"), PostHogSettingsValidation::Validate(*EmptyKeySettings).bIsValid);

	UPostHogDeveloperSettings* WhitespaceKeySettings = MakeTransientSettings();
	UnrealHogTests::SetPropertyValue<FString>(WhitespaceKeySettings, TEXT("ApiKey"), TEXT("   "));
	TestFalse(TEXT("Whitespace API key fails"), PostHogSettingsValidation::Validate(*WhitespaceKeySettings).bIsValid);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogSettingsBlankCustomHostNormalizesTest, "UnrealHog.Configuration.DeveloperSettings.BlankCustomHostNormalizesToUs", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogSettingsBlankCustomHostNormalizesTest::RunTest(const FString& Parameters)
{
	UPostHogDeveloperSettings* Settings = MakeTransientSettings();
	UnrealHogTests::SetPropertyValue<FString>(Settings, TEXT("ApiKey"), TEXT("phc_valid_key"));
	UnrealHogTests::SetPropertyValue<EPostHogHost>(Settings, TEXT("HostType"), EPostHogHost::Custom);
	UnrealHogTests::SetPropertyValue<FString>(Settings, TEXT("Host"), TEXT("   "));

	const FPostHogSettingsValidationResult Result = PostHogSettingsValidation::Validate(*Settings);
	TestTrue(TEXT("Blank custom host still validates"), Result.bIsValid);
	TestEqual(TEXT("Blank custom host normalizes to US host"), Result.ResolvedHost, TEXT("https://us.i.posthog.com"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogSettingsCustomHostCanonicalizesTest, "UnrealHog.Configuration.DeveloperSettings.CustomHostCanonicalizesTrailingSlashes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogSettingsCustomHostCanonicalizesTest::RunTest(const FString& Parameters)
{
	struct FHostCase
	{
		const TCHAR* InputHost;
		const TCHAR* ExpectedHost;
	};

	const FHostCase Cases[] = {
		{ TEXT("https://example.com"), TEXT("https://example.com") },
		{ TEXT("https://example.com/"), TEXT("https://example.com") },
		{ TEXT("https://example.com///"), TEXT("https://example.com") },
		{ TEXT("  https://example.com///  "), TEXT("https://example.com") }
	};

	for (const FHostCase& Case : Cases)
	{
		UPostHogDeveloperSettings* Settings = MakeTransientSettings();
		UnrealHogTests::SetPropertyValue<FString>(Settings, TEXT("ApiKey"), TEXT("phc_valid_key"));
		UnrealHogTests::SetPropertyValue<EPostHogHost>(Settings, TEXT("HostType"), EPostHogHost::Custom);
		UnrealHogTests::SetPropertyValue<FString>(Settings, TEXT("Host"), Case.InputHost);

		const FPostHogSettingsValidationResult Result = PostHogSettingsValidation::Validate(*Settings);
		TestTrue(*FString::Printf(TEXT("%s validates"), Case.InputHost), Result.bIsValid);
		TestEqual(*FString::Printf(TEXT("%s canonicalizes"), Case.InputHost), Result.ResolvedHost, Case.ExpectedHost);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogSettingsCustomHostInternalWhitespaceTest, "UnrealHog.Configuration.DeveloperSettings.CustomHostPreservesInternalWhitespace", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogSettingsCustomHostInternalWhitespaceTest::RunTest(const FString& Parameters)
{
	UPostHogDeveloperSettings* Settings = MakeTransientSettings();
	UnrealHogTests::SetPropertyValue<FString>(Settings, TEXT("ApiKey"), TEXT("phc_valid_key"));
	UnrealHogTests::SetPropertyValue<EPostHogHost>(Settings, TEXT("HostType"), EPostHogHost::Custom);
	UnrealHogTests::SetPropertyValue<FString>(Settings, TEXT("Host"), TEXT("https://exa mple.com///"));

	const FPostHogSettingsValidationResult Result = PostHogSettingsValidation::Validate(*Settings);
	TestTrue(TEXT("Custom host validates"), Result.bIsValid);
	TestEqual(TEXT("Custom host preserves internal whitespace"), Result.ResolvedHost, TEXT("https://exa mple.com"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogSettingsNumericBoundsTest, "UnrealHog.Configuration.DeveloperSettings.RejectsNonPositiveDeliveryLimits", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogSettingsNumericBoundsTest::RunTest(const FString& Parameters)
{
	const TCHAR* NumericFields[] = { TEXT("FlushEventCount"), TEXT("FlushIntervalSeconds"), TEXT("MaxQueueSize"), TEXT("MaxBatchSize") };
	const int32 InvalidValues[] = { 0, -1 };

	for (const TCHAR* FieldName : NumericFields)
	{
		for (const int32 InvalidValue : InvalidValues)
		{
			UPostHogDeveloperSettings* Settings = MakeTransientSettings();
			UnrealHogTests::SetPropertyValue<FString>(Settings, TEXT("ApiKey"), TEXT("phc_valid_key"));
			UnrealHogTests::SetPropertyValue<int32>(Settings, FieldName, InvalidValue);

			const bool bIsValid = PostHogSettingsValidation::Validate(*Settings).bIsValid;
			TestFalse(*FString::Printf(TEXT("%s = %d rejected"), FieldName, InvalidValue), bIsValid);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogPersonProfilesEnumTest, "UnrealHog.Configuration.DeveloperSettings.PersonProfilesRoundTrips", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogPersonProfilesEnumTest::RunTest(const FString& Parameters)
{
	UPostHogDeveloperSettings* Settings = MakeTransientSettings();

	UnrealHogTests::SetPropertyValue<EPostHogPersonProfiles>(Settings, TEXT("PersonProfiles"), EPostHogPersonProfiles::Always);
	TestTrue(TEXT("Can be set to Always"), UnrealHogTests::GetPropertyValue<EPostHogPersonProfiles>(Settings, TEXT("PersonProfiles")) == EPostHogPersonProfiles::Always);

	UnrealHogTests::SetPropertyValue<EPostHogPersonProfiles>(Settings, TEXT("PersonProfiles"), EPostHogPersonProfiles::Never);
	TestTrue(TEXT("Can be set to Never"), UnrealHogTests::GetPropertyValue<EPostHogPersonProfiles>(Settings, TEXT("PersonProfiles")) == EPostHogPersonProfiles::Never);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogLogLevelEnumTest, "UnrealHog.Configuration.DeveloperSettings.LogLevelRoundTrips", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogLogLevelEnumTest::RunTest(const FString& Parameters)
{
	UPostHogDeveloperSettings* Settings = MakeTransientSettings();

	UnrealHogTests::SetPropertyValue<EPostHogLogLevel>(Settings, TEXT("LogLevel"), EPostHogLogLevel::Debug);
	TestTrue(TEXT("Can be set to Debug"), UnrealHogTests::GetPropertyValue<EPostHogLogLevel>(Settings, TEXT("LogLevel")) == EPostHogLogLevel::Debug);

	UnrealHogTests::SetPropertyValue<EPostHogLogLevel>(Settings, TEXT("LogLevel"), EPostHogLogLevel::None);
	TestTrue(TEXT("Can be set to None"), UnrealHogTests::GetPropertyValue<EPostHogLogLevel>(Settings, TEXT("LogLevel")) == EPostHogLogLevel::None);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogSettingsConfigFlagsTest, "UnrealHog.Configuration.DeveloperSettings.KeyFieldsAreConfigBacked", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogSettingsConfigFlagsTest::RunTest(const FString& Parameters)
{
	const TCHAR* ConfigBackedFields[] = { TEXT("bAnalyticsEnabled"), TEXT("ApiKey"), TEXT("bDefaultUserOptIn"), TEXT("HostType"), TEXT("Host"), TEXT("FlushEventCount") };

	for (const TCHAR* FieldName : ConfigBackedFields)
	{
		const FProperty* Property = FindFProperty<FProperty>(UPostHogDeveloperSettings::StaticClass(), FieldName);
		TestNotNull(*FString::Printf(TEXT("%s exists"), FieldName), Property);

		if (Property)
		{
			TestTrue(*FString::Printf(TEXT("%s has CPF_Config"), FieldName), Property->HasAnyPropertyFlags(CPF_Config));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogUnavailableCapabilitySettingsAreDisabledAndAnnotatedTest, "UnrealHog.Configuration.DeveloperSettings.UnavailableCapabilitySettingsAreDisabledAndAnnotated", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogUnavailableCapabilitySettingsAreDisabledAndAnnotatedTest::RunTest(const FString& Parameters)
{
#if WITH_EDITOR
	UPostHogDeveloperSettings* Settings = MakeTransientSettings();

	const FProperty* HostProperty = FindFProperty<FProperty>(UPostHogDeveloperSettings::StaticClass(), TEXT("Host"));
	if (TestNotNull(TEXT("Host property exists"), HostProperty))
	{
		TestFalse(TEXT("Host is disabled for managed US host"), Settings->CanEditChange(HostProperty));
		UnrealHogTests::SetPropertyValue<EPostHogHost>(Settings, TEXT("HostType"), EPostHogHost::Custom);
		TestTrue(TEXT("Host is enabled for custom host"), Settings->CanEditChange(HostProperty));
	}

	const FProperty* AnalyticsFlushEventCountProperty = FindFProperty<FProperty>(UPostHogDeveloperSettings::StaticClass(), TEXT("FlushEventCount"));
	if (TestNotNull(TEXT("Analytics FlushEventCount property exists"), AnalyticsFlushEventCountProperty))
	{
		TestTrue(TEXT("Analytics FlushEventCount remains editable"), Settings->CanEditChange(AnalyticsFlushEventCountProperty));
	}

	const FProperty* AnalyticsMaxQueueSizeProperty = FindFProperty<FProperty>(UPostHogDeveloperSettings::StaticClass(), TEXT("MaxQueueSize"));
	if (TestNotNull(TEXT("Analytics MaxQueueSize property exists"), AnalyticsMaxQueueSizeProperty))
	{
		TestTrue(TEXT("Analytics MaxQueueSize remains editable"), Settings->CanEditChange(AnalyticsMaxQueueSizeProperty));
	}

	struct FUnavailablePropertyCase
	{
		UStruct* Owner;
		const TCHAR* PropertyName;
		const TCHAR* RequiredMetadataText;
	};

	const FUnavailablePropertyCase Cases[] = {
		{ UPostHogDeveloperSettings::StaticClass(), TEXT("bPreloadFeatureFlags"), TEXT("SDKP-012") },
		{ UPostHogDeveloperSettings::StaticClass(), TEXT("FeatureFlagRequestMaxRetries"), TEXT("SDKP-012") },
		{ UPostHogDeveloperSettings::StaticClass(), TEXT("bSendFeatureFlagEvent"), TEXT("SDKP-012") },
		{ UPostHogDeveloperSettings::StaticClass(), TEXT("bSendDefaultPersonPropertiesForFlags"), TEXT("SDKP-012") },
		{ UPostHogDeveloperSettings::StaticClass(), TEXT("bSessionReplay"), TEXT("SDKP-018") },
		{ UPostHogDeveloperSettings::StaticClass(), TEXT("SessionReplayConfig"), TEXT("SDKP-018") },
		{ FPostHogSessionReplayConfig::StaticStruct(), TEXT("ThrottleDelaySeconds"), TEXT("SDKP-018") },
		{ FPostHogSessionReplayConfig::StaticStruct(), TEXT("ScreenshotQuality"), TEXT("SDKP-018") },
		{ FPostHogSessionReplayConfig::StaticStruct(), TEXT("bCaptureNetworkTelemetry"), TEXT("SDKP-018") },
		{ FPostHogSessionReplayConfig::StaticStruct(), TEXT("bCaptureLogs"), TEXT("SDKP-018") },
		{ FPostHogSessionReplayConfig::StaticStruct(), TEXT("MinLogLevel"), TEXT("SDKP-018") },
		{ FPostHogSessionReplayConfig::StaticStruct(), TEXT("ScreenshotScale"), TEXT("SDKP-018") },
		{ FPostHogSessionReplayConfig::StaticStruct(), TEXT("FlushEventCount"), TEXT("SDKP-018") },
		{ FPostHogSessionReplayConfig::StaticStruct(), TEXT("FlushIntervalSeconds"), TEXT("SDKP-018") },
		{ FPostHogSessionReplayConfig::StaticStruct(), TEXT("MaxQueueSize"), TEXT("SDKP-018") }
	};

	for (const FUnavailablePropertyCase& Case : Cases)
	{
		const FProperty* Property = FindFProperty<FProperty>(Case.Owner, Case.PropertyName);
		if (!TestNotNull(*FString::Printf(TEXT("%s property exists"), Case.PropertyName), Property))
		{
			continue;
		}

		TestFalse(*FString::Printf(TEXT("%s cannot be edited"), Case.PropertyName), Settings->CanEditChange(Property));
		TestTrue(*FString::Printf(TEXT("%s tooltip names removal task"), Case.PropertyName), Property->GetMetaData(TEXT("ToolTip")).Contains(Case.RequiredMetadataText));
		TestTrue(*FString::Printf(TEXT("%s display name names removal task"), Case.PropertyName), Property->GetMetaData(TEXT("DisplayName")).Contains(Case.RequiredMetadataText));
	}

	return true;
#else
	AddError(TEXT("Unavailable capability editor metadata test requires WITH_EDITOR."));
	return false;
#endif
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogUnavailableCapabilitySerializedFieldsRemainCompatibleTest, "UnrealHog.Configuration.DeveloperSettings.UnavailableCapabilitySerializedFieldsRemainCompatible", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogUnavailableCapabilitySerializedFieldsRemainCompatibleTest::RunTest(const FString& Parameters)
{
	const TCHAR* ConfigBackedUnavailableFields[] = {
		TEXT("bPreloadFeatureFlags"),
		TEXT("FeatureFlagRequestMaxRetries"),
		TEXT("bSendFeatureFlagEvent"),
		TEXT("bSendDefaultPersonPropertiesForFlags"),
		TEXT("bSessionReplay"),
		TEXT("SessionReplayConfig")
	};

	for (const TCHAR* FieldName : ConfigBackedUnavailableFields)
	{
		const FProperty* Property = FindFProperty<FProperty>(UPostHogDeveloperSettings::StaticClass(), FieldName);
		TestNotNull(*FString::Printf(TEXT("%s exists"), FieldName), Property);

		if (Property)
		{
			TestTrue(*FString::Printf(TEXT("%s has CPF_Config"), FieldName), Property->HasAnyPropertyFlags(CPF_Config));
		}
	}

	UPostHogDeveloperSettings* Settings = MakeTransientSettings();
	TestTrue(TEXT("Default feature flag preload remains true"), Settings->ShouldPreloadFeatureFlags());
	TestEqual(TEXT("Default feature flag max retries remains one"), UnrealHogTests::GetPropertyValue<int32>(Settings, TEXT("FeatureFlagRequestMaxRetries")), 1);
	TestTrue(TEXT("Default feature flag event remains true"), UnrealHogTests::GetPropertyValue<bool>(Settings, TEXT("bSendFeatureFlagEvent")));
	TestTrue(TEXT("Default feature flag request properties remains true"), UnrealHogTests::GetPropertyValue<bool>(Settings, TEXT("bSendDefaultPersonPropertiesForFlags")));
	TestFalse(TEXT("Default session replay remains false"), Settings->IsSessionReplayEnabled());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogUnavailableCapabilityValidationReportsDiagnosticsWithoutBlockingAnalyticsTest, "UnrealHog.Configuration.DeveloperSettings.UnavailableCapabilityValidationReportsDiagnosticsWithoutBlockingAnalytics", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogUnavailableCapabilityValidationReportsDiagnosticsWithoutBlockingAnalyticsTest::RunTest(const FString& Parameters)
{
	UPostHogDeveloperSettings* Settings = MakeTransientSettings();
	UnrealHogTests::SetPropertyValue<FString>(Settings, TEXT("ApiKey"), TEXT("phc_valid_key"));

	const FPostHogSettingsValidationResult DefaultResult = PostHogSettingsValidation::Validate(*Settings);
	TestTrue(TEXT("Default unavailable feature-flag preload does not block analytics"), DefaultResult.bIsValid);
	TestTrue(TEXT("Default validation reports feature-flag preload unavailable"), DefaultResult.bFeatureFlagPreloadUnavailable);
	TestFalse(TEXT("Default validation does not report session replay unavailable"), DefaultResult.bSessionReplayUnavailable);
	TestEqual(TEXT("Default validation reports one unavailable diagnostic"), DefaultResult.UnavailableCapabilityDiagnostics.Num(), 1);
	if (DefaultResult.UnavailableCapabilityDiagnostics.Num() == 1)
	{
		TestTrue(TEXT("Feature diagnostic names SDKP-012"), DefaultResult.UnavailableCapabilityDiagnostics[0].Contains(TEXT("SDKP-012")));
		TestTrue(TEXT("Feature diagnostic names feature-flag preload"), DefaultResult.UnavailableCapabilityDiagnostics[0].Contains(TEXT("feature-flag preload")));
	}

	UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bSessionReplay"), true);
	const FPostHogSettingsValidationResult ReplayResult = PostHogSettingsValidation::Validate(*Settings);
	TestTrue(TEXT("Unavailable feature flags and replay do not block analytics"), ReplayResult.bIsValid);
	TestTrue(TEXT("Replay validation reports feature-flag preload unavailable"), ReplayResult.bFeatureFlagPreloadUnavailable);
	TestTrue(TEXT("Replay validation reports session replay unavailable"), ReplayResult.bSessionReplayUnavailable);
	TestEqual(TEXT("Replay validation reports two unavailable diagnostics"), ReplayResult.UnavailableCapabilityDiagnostics.Num(), 2);

	UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bPreloadFeatureFlags"), false);
	UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bSessionReplay"), false);
	const FPostHogSettingsValidationResult DisabledResult = PostHogSettingsValidation::Validate(*Settings);
	TestTrue(TEXT("Disabled unavailable settings keep analytics valid"), DisabledResult.bIsValid);
	TestFalse(TEXT("Disabled validation does not report feature-flag preload"), DisabledResult.bFeatureFlagPreloadUnavailable);
	TestFalse(TEXT("Disabled validation does not report session replay"), DisabledResult.bSessionReplayUnavailable);
	TestEqual(TEXT("Disabled validation reports no unavailable diagnostics"), DisabledResult.UnavailableCapabilityDiagnostics.Num(), 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
