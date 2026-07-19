#include "Consent/PostHogConsentController.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Events/PostHogBatchPayload.h"
#include "Events/PostHogCapturePolicy.h"
#include "PostHogDeveloperSettings.h"
#include "Storage/PostHogFileStorageProvider.h"
#include "Tests/PostHogFakeBatchTransport.h"
#include "Tests/PostHogTestPropertyHelpers.h"
#include "UObject/Package.h"

namespace
{
	// RAII fixture that owns a unique temporary directory for the file storage provider backing
	// these person-profile policy tests; removed on scope exit even if an assertion fails mid-test.
	class FScopedPersonProfilePolicyTestStorageDirectory
	{
	public:
		FScopedPersonProfilePolicyTestStorageDirectory()
			: RootPath(FPaths::Combine(FPlatformProcess::UserTempDir(), TEXT("UnrealHogPersonProfilePolicyTests"), FGuid::NewGuid().ToString(EGuidFormats::Digits)))
		{
		}

		~FScopedPersonProfilePolicyTestStorageDirectory()
		{
			IFileManager::Get().DeleteDirectory(*RootPath, false, true);
		}

		const FString& GetRootPath() const { return RootPath; }

	private:
		FString RootPath;
	};

	UPostHogDeveloperSettings* MakePersonProfilePolicyTestSettings(EPostHogPersonProfiles PersonProfiles)
	{
		UPostHogDeveloperSettings* Settings = NewObject<UPostHogDeveloperSettings>(GetTransientPackage());
		UnrealHogTests::SetPropertyValue<FString>(Settings, TEXT("ApiKey"), TEXT("phc_valid_key"));
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bAnalyticsEnabled"), true);
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bDefaultUserOptIn"), false);
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bCaptureApplicationLifecycleEvents"), false);
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bPreloadFeatureFlags"), false);
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bSessionReplay"), false);
		UnrealHogTests::SetPropertyValue<EPostHogPersonProfiles>(Settings, TEXT("PersonProfiles"), PersonProfiles);
		return Settings;
	}

	FPostHogConsentController::FStorageProviderFactory MakePersonProfilePolicyStorageFactory(const FString& RootPath)
	{
		return [RootPath]() -> TUniquePtr<IPostHogStorageProvider>
		{
			return MakeUnique<FPostHogFileStorageProvider>(RootPath);
		};
	}

	// Captures the most recently created fake transport so tests can drive its completion callbacks.
	FPostHogConsentController::FTransportFactory MakePersonProfilePolicyTransportFactory(FPostHogFakeBatchTransport*& OutLastTransport)
	{
		return [&OutLastTransport](const FString&) -> TUniquePtr<IPostHogBatchTransport>
		{
			TUniquePtr<FPostHogFakeBatchTransport> Transport = MakeUnique<FPostHogFakeBatchTransport>();
			OutLastTransport = Transport.Get();
			return Transport;
		};
	}

	// Deterministic, countable stand-in for PostHogUuidV7::New().
	FPostHogConsentController::FUuidGenerator MakePersonProfilePolicyUuidGenerator(int32& Counter)
	{
		return [&Counter]() { return FString::Printf(TEXT("person-profile-%d"), ++Counter); };
	}

	bool TryGetSinglePayloadEventProperties(const FPostHogBatchPayload& Payload, TSharedPtr<FJsonObject>& OutProperties)
	{
		const TSharedRef<FJsonObject> PayloadJson = Payload.ToJsonObject();

		const TArray<TSharedPtr<FJsonValue>>* BatchArray = nullptr;
		if (!PayloadJson->TryGetArrayField(TEXT("batch"), BatchArray) || BatchArray->Num() != 1)
		{
			return false;
		}

		const TSharedPtr<FJsonObject> EventObject = (*BatchArray)[0]->AsObject();
		if (!EventObject.IsValid())
		{
			return false;
		}

		const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
		if (!EventObject->TryGetObjectField(TEXT("properties"), PropertiesObject) || !PropertiesObject->IsValid())
		{
			return false;
		}

		OutProperties = *PropertiesObject;
		return true;
	}

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogPersonProfilePolicyMatrixTest, "UnrealHog.PersonProfilePolicy.ShouldProcessPersonProfileMatchesPolicyIdentityMatrix", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogPersonProfilePolicyMatrixTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("Never + anonymous is false"), PostHogCapturePolicy::ShouldProcessPersonProfile(EPostHogPersonProfiles::Never, false));
	TestFalse(TEXT("Never + identified is false"), PostHogCapturePolicy::ShouldProcessPersonProfile(EPostHogPersonProfiles::Never, true));

	TestFalse(TEXT("IdentifiedOnly + anonymous is false"), PostHogCapturePolicy::ShouldProcessPersonProfile(EPostHogPersonProfiles::IdentifiedOnly, false));
	TestTrue(TEXT("IdentifiedOnly + identified is true"), PostHogCapturePolicy::ShouldProcessPersonProfile(EPostHogPersonProfiles::IdentifiedOnly, true));

	TestTrue(TEXT("Always + anonymous is true"), PostHogCapturePolicy::ShouldProcessPersonProfile(EPostHogPersonProfiles::Always, false));
	TestTrue(TEXT("Always + identified is true"), PostHogCapturePolicy::ShouldProcessPersonProfile(EPostHogPersonProfiles::Always, true));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogPersonProfilePolicyIdentifiedOnlyAnonymousIsProfilelessTest, "UnrealHog.PersonProfilePolicy.IdentifiedOnlyAnonymousCaptureIsExplicitlyProfileless", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogPersonProfilePolicyIdentifiedOnlyAnonymousIsProfilelessTest::RunTest(const FString& Parameters)
{
	FScopedPersonProfilePolicyTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(
		MakePersonProfilePolicyStorageFactory(Fixture.GetRootPath()),
		MakePersonProfilePolicyTransportFactory(LastTransport),
		MakePersonProfilePolicyUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakePersonProfilePolicyTestSettings(EPostHogPersonProfiles::IdentifiedOnly);

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);

	Controller.CaptureEvent(TEXT("anon-event"), nullptr);
	Controller.Flush();
	if (!TestNotNull(TEXT("Transport created"), LastTransport))
	{
		return false;
	}

	TSharedPtr<FJsonObject> PropertiesObject;
	TestTrue(TEXT("Payload parsed"), TryGetSinglePayloadEventProperties(LastTransport->GetLastPayload(), PropertiesObject));
	LastTransport->CompleteLast(true, 200, TEXT(""));

	bool bProcessPersonProfileValue = true;
	TestTrue(TEXT("properties has $process_person_profile"), PropertiesObject->TryGetBoolField(TEXT("$process_person_profile"), bProcessPersonProfileValue));
	TestFalse(TEXT("Default IdentifiedOnly anonymous capture is explicitly profileless"), bProcessPersonProfileValue);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogPersonProfilePolicyAlwaysOmitsKeyTest, "UnrealHog.PersonProfilePolicy.AlwaysPolicyOmitsProcessPersonProfileKey", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogPersonProfilePolicyAlwaysOmitsKeyTest::RunTest(const FString& Parameters)
{
	FScopedPersonProfilePolicyTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(
		MakePersonProfilePolicyStorageFactory(Fixture.GetRootPath()),
		MakePersonProfilePolicyTransportFactory(LastTransport),
		MakePersonProfilePolicyUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakePersonProfilePolicyTestSettings(EPostHogPersonProfiles::Always);

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);

	Controller.CaptureEvent(TEXT("always-event"), nullptr);
	Controller.Flush();
	if (!TestNotNull(TEXT("Transport created"), LastTransport))
	{
		return false;
	}

	TSharedPtr<FJsonObject> PropertiesObject;
	TestTrue(TEXT("Payload parsed"), TryGetSinglePayloadEventProperties(LastTransport->GetLastPayload(), PropertiesObject));
	LastTransport->CompleteLast(true, 200, TEXT(""));

	TestFalse(TEXT("Always policy omits $process_person_profile entirely"), PropertiesObject->HasField(TEXT("$process_person_profile")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogPersonProfilePolicyIdentifiedOnlyIdentifiedOmitsKeyTest, "UnrealHog.PersonProfilePolicy.IdentifiedOnlyIdentifiedCaptureOmitsProcessPersonProfileKey", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogPersonProfilePolicyIdentifiedOnlyIdentifiedOmitsKeyTest::RunTest(const FString& Parameters)
{
	FScopedPersonProfilePolicyTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(
		MakePersonProfilePolicyStorageFactory(Fixture.GetRootPath()),
		MakePersonProfilePolicyTransportFactory(LastTransport),
		MakePersonProfilePolicyUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakePersonProfilePolicyTestSettings(EPostHogPersonProfiles::IdentifiedOnly);

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);

	Controller.Identify(TEXT("known-user"), nullptr, nullptr);
	Controller.Flush();
	if (!TestNotNull(TEXT("Transport created"), LastTransport))
	{
		return false;
	}

	TSharedPtr<FJsonObject> IdentifyPropertiesObject;
	TestTrue(TEXT("Identify payload parsed"), TryGetSinglePayloadEventProperties(LastTransport->GetLastPayload(), IdentifyPropertiesObject));
	TestFalse(TEXT("$identify itself omits $process_person_profile since identity flips before policy computation"), IdentifyPropertiesObject->HasField(TEXT("$process_person_profile")));
	LastTransport->CompleteLast(true, 200, TEXT(""));

	Controller.CaptureEvent(TEXT("post-identify-event"), nullptr);
	Controller.Flush();

	TSharedPtr<FJsonObject> PropertiesObject;
	TestTrue(TEXT("Payload parsed"), TryGetSinglePayloadEventProperties(LastTransport->GetLastPayload(), PropertiesObject));
	LastTransport->CompleteLast(true, 200, TEXT(""));

	TestFalse(TEXT("Identified capture under IdentifiedOnly omits $process_person_profile"), PropertiesObject->HasField(TEXT("$process_person_profile")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogPersonProfilePolicySnapshotIsolationTest, "UnrealHog.PersonProfilePolicy.CaptureReadsSnapshotNotLiveSettings", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogPersonProfilePolicySnapshotIsolationTest::RunTest(const FString& Parameters)
{
	FScopedPersonProfilePolicyTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(
		MakePersonProfilePolicyStorageFactory(Fixture.GetRootPath()),
		MakePersonProfilePolicyTransportFactory(LastTransport),
		MakePersonProfilePolicyUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakePersonProfilePolicyTestSettings(EPostHogPersonProfiles::IdentifiedOnly);

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);

	Controller.CaptureEvent(TEXT("before-mutate-event"), nullptr);
	Controller.Flush();
	if (!TestNotNull(TEXT("Transport created"), LastTransport))
	{
		return false;
	}

	TSharedPtr<FJsonObject> FirstPropertiesObject;
	TestTrue(TEXT("First payload parsed"), TryGetSinglePayloadEventProperties(LastTransport->GetLastPayload(), FirstPropertiesObject));
	LastTransport->CompleteLast(true, 200, TEXT(""));

	bool bFirstValue = true;
	TestTrue(TEXT("First properties has $process_person_profile"), FirstPropertiesObject->TryGetBoolField(TEXT("$process_person_profile"), bFirstValue));
	TestFalse(TEXT("First anonymous capture is profileless"), bFirstValue);

	// Mutate the live Settings object without re-Initialize/SetOptIn: the controller must not
	// re-read the mutable CDO/Settings snapshot on every capture.
	UnrealHogTests::SetPropertyValue<EPostHogPersonProfiles>(Settings, TEXT("PersonProfiles"), EPostHogPersonProfiles::Always);

	Controller.CaptureEvent(TEXT("after-mutate-event"), nullptr);
	Controller.Flush();

	TSharedPtr<FJsonObject> SecondPropertiesObject;
	TestTrue(TEXT("Second payload parsed"), TryGetSinglePayloadEventProperties(LastTransport->GetLastPayload(), SecondPropertiesObject));
	LastTransport->CompleteLast(true, 200, TEXT(""));

	bool bSecondValue = true;
	TestTrue(TEXT("Second properties still has $process_person_profile"), SecondPropertiesObject->TryGetBoolField(TEXT("$process_person_profile"), bSecondValue));
	TestFalse(TEXT("Controller still uses its captured snapshot, unaffected by the live Settings mutation"), bSecondValue);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
