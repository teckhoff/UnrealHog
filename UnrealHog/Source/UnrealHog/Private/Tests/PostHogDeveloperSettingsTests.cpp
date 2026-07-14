// Trevor Eckhoff, 2026. All rights reserved.

#include "PostHogDeveloperSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Logging/PostHogLogger.h"
#include "PostHogSettingsValidation.h"
#include "UObject/UnrealType.h"

namespace
{
	UPostHogDeveloperSettings* MakeTransientSettings()
	{
		return NewObject<UPostHogDeveloperSettings>(GetTransientPackage());
	}

	template<typename T>
	void SetPropertyValue(UObject* Object, const TCHAR* PropertyName, const T& Value)
	{
		FProperty* Property = FindFProperty<FProperty>(Object->GetClass(), PropertyName);
		check(Property);
		T* ValuePtr = Property->ContainerPtrToValuePtr<T>(Object);
		*ValuePtr = Value;
	}

	template<typename T>
	T GetPropertyValue(const UObject* Object, const TCHAR* PropertyName)
	{
		const FProperty* Property = FindFProperty<FProperty>(Object->GetClass(), PropertyName);
		check(Property);
		return *Property->ContainerPtrToValuePtr<T>(Object);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogSettingsDefaultsTest, "UnrealHog.Configuration.DeveloperSettings.DefaultsAreCorrect", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogSettingsDefaultsTest::RunTest(const FString& Parameters)
{
	UPostHogDeveloperSettings* Settings = MakeTransientSettings();

	// Repository invariant: analytics collection is opt-in, diverging from the Unity default.
	TestFalse(TEXT("Analytics disabled by default"), Settings->IsAnalyticsEnabled());

	TestEqual(TEXT("Default flush event count"), Settings->GetFlushEventCount(), 20);
	TestEqual(TEXT("Default flush interval seconds"), Settings->GetFlushIntervalSeconds(), 30);
	TestEqual(TEXT("Default max queue size"), Settings->GetMaxQueueSize(), 1000);
	TestEqual(TEXT("Default max batch size"), Settings->GetMaxBatchSize(), 50);
	TestTrue(TEXT("Default lifecycle capture enabled"), Settings->ShouldCaptureApplicationLifecycleEvents());

	TestTrue(TEXT("Default person profiles"), GetPropertyValue<EPostHogPersonProfiles>(Settings, TEXT("PersonProfiles")) == EPostHogPersonProfiles::IdentifiedOnly);
	TestTrue(TEXT("Default log level"), GetPropertyValue<EPostHogLogLevel>(Settings, TEXT("LogLevel")) == EPostHogLogLevel::Warning);
	TestFalse(TEXT("Default no anonymous ID reuse"), GetPropertyValue<bool>(Settings, TEXT("bReuseAnonymousId")));
	TestTrue(TEXT("Default flush on quit enabled"), GetPropertyValue<bool>(Settings, TEXT("bFlushOnQuit")));
	TestEqual(TEXT("Default flush on quit timeout"), GetPropertyValue<float>(Settings, TEXT("FlushOnQuitTimeoutSeconds")), 3.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogSettingsHostResolutionTest, "UnrealHog.Configuration.DeveloperSettings.ResolvesUsAndEuHosts", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogSettingsHostResolutionTest::RunTest(const FString& Parameters)
{
	UPostHogDeveloperSettings* Settings = MakeTransientSettings();

	SetPropertyValue<EPostHogHost>(Settings, TEXT("HostType"), EPostHogHost::US);
	TestEqual(TEXT("US resolves"), Settings->GetResolvedHost(), TEXT("https://us.i.posthog.com"));

	SetPropertyValue<EPostHogHost>(Settings, TEXT("HostType"), EPostHogHost::EU);
	TestEqual(TEXT("EU resolves"), Settings->GetResolvedHost(), TEXT("https://eu.i.posthog.com"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogSettingsValidConfigTest, "UnrealHog.Configuration.DeveloperSettings.ValidConfigPasses", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogSettingsValidConfigTest::RunTest(const FString& Parameters)
{
	UPostHogDeveloperSettings* Settings = MakeTransientSettings();
	SetPropertyValue<FString>(Settings, TEXT("ApiKey"), TEXT("phc_valid_key"));

	const FPostHogSettingsValidationResult Result = PostHogSettingsValidation::Validate(*Settings);
	TestTrue(TEXT("Valid config passes"), Result.bIsValid);
	TestEqual(TEXT("Resolved host defaults to US"), Result.ResolvedHost, TEXT("https://us.i.posthog.com"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogSettingsInvalidApiKeyTest, "UnrealHog.Configuration.DeveloperSettings.EmptyOrWhitespaceApiKeyFails", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogSettingsInvalidApiKeyTest::RunTest(const FString& Parameters)
{
	UPostHogDeveloperSettings* EmptyKeySettings = MakeTransientSettings();
	SetPropertyValue<FString>(EmptyKeySettings, TEXT("ApiKey"), TEXT(""));
	TestFalse(TEXT("Empty API key fails"), PostHogSettingsValidation::Validate(*EmptyKeySettings).bIsValid);

	UPostHogDeveloperSettings* WhitespaceKeySettings = MakeTransientSettings();
	SetPropertyValue<FString>(WhitespaceKeySettings, TEXT("ApiKey"), TEXT("   "));
	TestFalse(TEXT("Whitespace API key fails"), PostHogSettingsValidation::Validate(*WhitespaceKeySettings).bIsValid);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogSettingsBlankCustomHostNormalizesTest, "UnrealHog.Configuration.DeveloperSettings.BlankCustomHostNormalizesToUs", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogSettingsBlankCustomHostNormalizesTest::RunTest(const FString& Parameters)
{
	UPostHogDeveloperSettings* Settings = MakeTransientSettings();
	SetPropertyValue<FString>(Settings, TEXT("ApiKey"), TEXT("phc_valid_key"));
	SetPropertyValue<EPostHogHost>(Settings, TEXT("HostType"), EPostHogHost::Custom);
	SetPropertyValue<FString>(Settings, TEXT("Host"), TEXT("   "));

	const FPostHogSettingsValidationResult Result = PostHogSettingsValidation::Validate(*Settings);
	TestTrue(TEXT("Blank custom host still validates"), Result.bIsValid);
	TestEqual(TEXT("Blank custom host normalizes to US host"), Result.ResolvedHost, TEXT("https://us.i.posthog.com"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogSettingsCustomHostUnchangedTest, "UnrealHog.Configuration.DeveloperSettings.NonEmptyCustomHostPasses", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogSettingsCustomHostUnchangedTest::RunTest(const FString& Parameters)
{
	UPostHogDeveloperSettings* Settings = MakeTransientSettings();
	SetPropertyValue<FString>(Settings, TEXT("ApiKey"), TEXT("phc_valid_key"));
	SetPropertyValue<EPostHogHost>(Settings, TEXT("HostType"), EPostHogHost::Custom);
	SetPropertyValue<FString>(Settings, TEXT("Host"), TEXT("  https://custom.example.com/  "));

	const FPostHogSettingsValidationResult Result = PostHogSettingsValidation::Validate(*Settings);
	TestTrue(TEXT("Custom host validates"), Result.bIsValid);
	TestEqual(TEXT("Custom host trimmed of whitespace and trailing slash"), Result.ResolvedHost, TEXT("https://custom.example.com"));

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
			SetPropertyValue<FString>(Settings, TEXT("ApiKey"), TEXT("phc_valid_key"));
			SetPropertyValue<int32>(Settings, FieldName, InvalidValue);

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

	SetPropertyValue<EPostHogPersonProfiles>(Settings, TEXT("PersonProfiles"), EPostHogPersonProfiles::Always);
	TestTrue(TEXT("Can be set to Always"), GetPropertyValue<EPostHogPersonProfiles>(Settings, TEXT("PersonProfiles")) == EPostHogPersonProfiles::Always);

	SetPropertyValue<EPostHogPersonProfiles>(Settings, TEXT("PersonProfiles"), EPostHogPersonProfiles::Never);
	TestTrue(TEXT("Can be set to Never"), GetPropertyValue<EPostHogPersonProfiles>(Settings, TEXT("PersonProfiles")) == EPostHogPersonProfiles::Never);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogLogLevelEnumTest, "UnrealHog.Configuration.DeveloperSettings.LogLevelRoundTrips", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogLogLevelEnumTest::RunTest(const FString& Parameters)
{
	UPostHogDeveloperSettings* Settings = MakeTransientSettings();

	SetPropertyValue<EPostHogLogLevel>(Settings, TEXT("LogLevel"), EPostHogLogLevel::Debug);
	TestTrue(TEXT("Can be set to Debug"), GetPropertyValue<EPostHogLogLevel>(Settings, TEXT("LogLevel")) == EPostHogLogLevel::Debug);

	SetPropertyValue<EPostHogLogLevel>(Settings, TEXT("LogLevel"), EPostHogLogLevel::None);
	TestTrue(TEXT("Can be set to None"), GetPropertyValue<EPostHogLogLevel>(Settings, TEXT("LogLevel")) == EPostHogLogLevel::None);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogSettingsConfigFlagsTest, "UnrealHog.Configuration.DeveloperSettings.KeyFieldsAreConfigBacked", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogSettingsConfigFlagsTest::RunTest(const FString& Parameters)
{
	const TCHAR* ConfigBackedFields[] = { TEXT("bAnalyticsEnabled"), TEXT("ApiKey"), TEXT("HostType"), TEXT("Host"), TEXT("FlushEventCount") };

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

#endif // WITH_DEV_AUTOMATION_TESTS
