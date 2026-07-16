#include "Consent/PostHogConsentController.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Events/PostHogBatchPayload.h"
#include "Events/PostHogEventProperties.h"
#include "PostHogDeveloperSettings.h"
#include "Storage/PostHogFileStorageProvider.h"
#include "Tests/PostHogFakeBatchTransport.h"
#include "Tests/PostHogTestPropertyHelpers.h"
#include "UObject/Package.h"

namespace
{
	// RAII fixture that owns a unique temporary directory for the file storage provider backing
	// these identity tests; removed on scope exit even if an assertion fails mid-test.
	class FScopedIdentityTestStorageDirectory
	{
	public:
		FScopedIdentityTestStorageDirectory()
			: RootPath(FPaths::Combine(FPlatformProcess::UserTempDir(), TEXT("UnrealHogIdentityTests"), FGuid::NewGuid().ToString(EGuidFormats::Digits)))
		{
		}

		~FScopedIdentityTestStorageDirectory()
		{
			IFileManager::Get().DeleteDirectory(*RootPath, false, true);
		}

		const FString& GetRootPath() const { return RootPath; }

	private:
		FString RootPath;
	};

	UPostHogDeveloperSettings* MakeIdentityTestSettings(bool bReuseAnonymousId)
	{
		UPostHogDeveloperSettings* Settings = NewObject<UPostHogDeveloperSettings>(GetTransientPackage());
		UnrealHogTests::SetPropertyValue<FString>(Settings, TEXT("ApiKey"), TEXT("phc_valid_key"));
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bAnalyticsEnabled"), true);
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bDefaultUserOptIn"), false);
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bCaptureApplicationLifecycleEvents"), false);
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bReuseAnonymousId"), bReuseAnonymousId);
		return Settings;
	}

	FPostHogConsentController::FStorageProviderFactory MakeIdentityStorageFactory(const FString& RootPath)
	{
		return [RootPath]() -> TUniquePtr<IPostHogStorageProvider>
		{
			return MakeUnique<FPostHogFileStorageProvider>(RootPath);
		};
	}

	// Captures the most recently created fake transport so tests can drive its completion callbacks.
	FPostHogConsentController::FTransportFactory MakeIdentityTransportFactory(FPostHogFakeBatchTransport*& OutLastTransport)
	{
		return [&OutLastTransport](const FString&) -> TUniquePtr<IPostHogBatchTransport>
		{
			TUniquePtr<FPostHogFakeBatchTransport> Transport = MakeUnique<FPostHogFakeBatchTransport>();
			OutLastTransport = Transport.Get();
			return Transport;
		};
	}

	// Deterministic, countable stand-in for PostHogUuidV7::New().
	FPostHogConsentController::FUuidGenerator MakeIdentityUuidGenerator(int32& Counter)
	{
		return [&Counter]() { return FString::Printf(TEXT("identity-uuid-%d"), ++Counter); };
	}

	bool TryGetIdentityPayloadEvents(const FPostHogBatchPayload& Payload, TArray<TSharedPtr<FJsonObject>>& OutEvents)
	{
		const TSharedRef<FJsonObject> PayloadJson = Payload.ToJsonObject();

		const TArray<TSharedPtr<FJsonValue>>* BatchArray = nullptr;
		if (!PayloadJson->TryGetArrayField(TEXT("batch"), BatchArray))
		{
			return false;
		}

		OutEvents.Reset(BatchArray->Num());
		for (const TSharedPtr<FJsonValue>& EventValue : *BatchArray)
		{
			const TSharedPtr<FJsonObject> EventObject = EventValue->AsObject();
			if (!EventObject.IsValid())
			{
				return false;
			}
			OutEvents.Add(EventObject);
		}

		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerIdentifyBlankNoOpTest, "UnrealHog.Consent.ConsentController.IdentifyBlankDistinctIdIsNoOp", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerIdentifyBlankNoOpTest::RunTest(const FString& Parameters)
{
	FScopedIdentityTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeIdentityStorageFactory(Fixture.GetRootPath()), MakeIdentityTransportFactory(LastTransport), MakeIdentityUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeIdentityTestSettings(false);

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);
	const FString DistinctIdBefore = Controller.GetDistinctId();

	const EPostHogCaptureResult Result = Controller.Identify(TEXT("   "), nullptr, nullptr);

	TestEqual(TEXT("Blank Identify is rejected"), Result, EPostHogCaptureResult::InvalidEventName);
	TestEqual(TEXT("No events queued"), Controller.GetQueuedEventCount(), 0);
	TestEqual(TEXT("Distinct id unchanged"), Controller.GetDistinctId(), DistinctIdBefore);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerAliasBlankNoOpTest, "UnrealHog.Consent.ConsentController.AliasBlankIsNoOp", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerAliasBlankNoOpTest::RunTest(const FString& Parameters)
{
	FScopedIdentityTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeIdentityStorageFactory(Fixture.GetRootPath()), MakeIdentityTransportFactory(LastTransport), MakeIdentityUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeIdentityTestSettings(false);

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);

	const EPostHogCaptureResult Result = Controller.Alias(TEXT("  "));

	TestEqual(TEXT("Blank Alias is rejected"), Result, EPostHogCaptureResult::InvalidEventName);
	TestEqual(TEXT("No events queued"), Controller.GetQueuedEventCount(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerIdentifyRequiresConsentTest, "UnrealHog.Consent.ConsentController.IdentifyRequiresConsent", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerIdentifyRequiresConsentTest::RunTest(const FString& Parameters)
{
	FScopedIdentityTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeIdentityStorageFactory(Fixture.GetRootPath()), MakeIdentityTransportFactory(LastTransport), MakeIdentityUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeIdentityTestSettings(false);

	Controller.Initialize(*Settings);

	const EPostHogCaptureResult Result = Controller.Identify(TEXT("user-1"), nullptr, nullptr);

	TestEqual(TEXT("Identify without consent is rejected"), Result, EPostHogCaptureResult::NotOptedIn);
	TestEqual(TEXT("No events queued"), Controller.GetQueuedEventCount(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerFirstIdentifyLinksAnonymousTest, "UnrealHog.Consent.ConsentController.FirstIdentifyEmitsAnonDistinctIdAndDistinctId", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerFirstIdentifyLinksAnonymousTest::RunTest(const FString& Parameters)
{
	FScopedIdentityTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeIdentityStorageFactory(Fixture.GetRootPath()), MakeIdentityTransportFactory(LastTransport), MakeIdentityUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeIdentityTestSettings(false);

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);
	const FString AnonymousId = Controller.GetDistinctId();

	const EPostHogCaptureResult Result = Controller.Identify(TEXT("user-1"), nullptr, nullptr);
	TestEqual(TEXT("Identify succeeds"), Result, EPostHogCaptureResult::Success);
	TestEqual(TEXT("Distinct id is now the identified id"), Controller.GetDistinctId(), FString(TEXT("user-1")));

	Controller.Flush();
	if (!TestNotNull(TEXT("Transport created"), LastTransport))
	{
		return false;
	}

	TArray<TSharedPtr<FJsonObject>> Events;
	TestTrue(TEXT("Payload parsed"), TryGetIdentityPayloadEvents(LastTransport->GetLastPayload(), Events));
	LastTransport->CompleteLast(true, 200, TEXT(""));

	TestEqual(TEXT("One event queued"), Events.Num(), 1);
	if (Events.Num() != 1)
	{
		return false;
	}

	FString EventName;
	TestTrue(TEXT("Event has name"), Events[0]->TryGetStringField(TEXT("event"), EventName));
	TestEqual(TEXT("Event is $identify"), EventName, FString(TEXT("$identify")));

	FString EventDistinctId;
	TestTrue(TEXT("Event has distinct_id"), Events[0]->TryGetStringField(TEXT("distinct_id"), EventDistinctId));
	TestEqual(TEXT("Event distinct_id is the new identity"), EventDistinctId, FString(TEXT("user-1")));

	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("Event has properties"), Events[0]->TryGetObjectField(TEXT("properties"), PropertiesObject));

	FString AnonDistinctId;
	TestTrue(TEXT("properties has $anon_distinct_id"), (*PropertiesObject)->TryGetStringField(TEXT("$anon_distinct_id"), AnonDistinctId));
	TestEqual(TEXT("$anon_distinct_id is the pre-identify anonymous id"), AnonDistinctId, AnonymousId);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerSecondIdentifyDoesNotRelinkTest, "UnrealHog.Consent.ConsentController.SecondIdentifyOmitsAnonDistinctId", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerSecondIdentifyDoesNotRelinkTest::RunTest(const FString& Parameters)
{
	FScopedIdentityTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeIdentityStorageFactory(Fixture.GetRootPath()), MakeIdentityTransportFactory(LastTransport), MakeIdentityUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeIdentityTestSettings(false);

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);

	Controller.Identify(TEXT("user-1"), nullptr, nullptr);
	Controller.Flush();
	if (!TestNotNull(TEXT("Transport created"), LastTransport))
	{
		return false;
	}
	LastTransport->CompleteLast(true, 200, TEXT(""));

	const EPostHogCaptureResult Result = Controller.Identify(TEXT("user-2"), nullptr, nullptr);
	TestEqual(TEXT("Second identify succeeds"), Result, EPostHogCaptureResult::Success);

	Controller.Flush();
	TArray<TSharedPtr<FJsonObject>> Events;
	TestTrue(TEXT("Payload parsed"), TryGetIdentityPayloadEvents(LastTransport->GetLastPayload(), Events));
	LastTransport->CompleteLast(true, 200, TEXT(""));

	TestEqual(TEXT("One event queued in second flush"), Events.Num(), 1);
	if (Events.Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("Event has properties"), Events[0]->TryGetObjectField(TEXT("properties"), PropertiesObject));
	TestFalse(TEXT("Second identify omits $anon_distinct_id (no relink)"), (*PropertiesObject)->HasField(TEXT("$anon_distinct_id")));

	FString EventDistinctId;
	TestTrue(TEXT("Event has distinct_id"), Events[0]->TryGetStringField(TEXT("distinct_id"), EventDistinctId));
	TestEqual(TEXT("Event distinct_id is the latest identity"), EventDistinctId, FString(TEXT("user-2")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerIdentifyNestsSetPropertiesTest, "UnrealHog.Consent.ConsentController.IdentifyNestsSetAndSetOnceProperties", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerIdentifyNestsSetPropertiesTest::RunTest(const FString& Parameters)
{
	FScopedIdentityTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeIdentityStorageFactory(Fixture.GetRootPath()), MakeIdentityTransportFactory(LastTransport), MakeIdentityUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeIdentityTestSettings(false);

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);

	UPostHogEventProperties* SetProperties = NewObject<UPostHogEventProperties>();
	SetProperties->AddString(TEXT("name"), TEXT("Ada"));

	UPostHogEventProperties* SetOnceProperties = NewObject<UPostHogEventProperties>();
	SetOnceProperties->AddString(TEXT("first_seen"), TEXT("2026-01-01"));

	const EPostHogCaptureResult Result = Controller.Identify(TEXT("user-1"), SetProperties, SetOnceProperties);
	TestEqual(TEXT("Identify succeeds"), Result, EPostHogCaptureResult::Success);

	Controller.Flush();
	if (!TestNotNull(TEXT("Transport created"), LastTransport))
	{
		return false;
	}

	TArray<TSharedPtr<FJsonObject>> Events;
	TestTrue(TEXT("Payload parsed"), TryGetIdentityPayloadEvents(LastTransport->GetLastPayload(), Events));
	LastTransport->CompleteLast(true, 200, TEXT(""));

	TestEqual(TEXT("One event queued"), Events.Num(), 1);
	if (Events.Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("Event has properties"), Events[0]->TryGetObjectField(TEXT("properties"), PropertiesObject));

	const TSharedPtr<FJsonObject>* SetObject = nullptr;
	TestTrue(TEXT("properties has $set"), (*PropertiesObject)->TryGetObjectField(TEXT("$set"), SetObject));
	FString NameValue;
	TestTrue(TEXT("$set has name"), (*SetObject)->TryGetStringField(TEXT("name"), NameValue));
	TestEqual(TEXT("$set.name is correct"), NameValue, FString(TEXT("Ada")));

	const TSharedPtr<FJsonObject>* SetOnceObject = nullptr;
	TestTrue(TEXT("properties has $set_once"), (*PropertiesObject)->TryGetObjectField(TEXT("$set_once"), SetOnceObject));
	FString FirstSeenValue;
	TestTrue(TEXT("$set_once has first_seen"), (*SetOnceObject)->TryGetStringField(TEXT("first_seen"), FirstSeenValue));
	TestEqual(TEXT("$set_once.first_seen is correct"), FirstSeenValue, FString(TEXT("2026-01-01")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerAliasEmitsCreateAliasTest, "UnrealHog.Consent.ConsentController.AliasEmitsCreateAliasEvent", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerAliasEmitsCreateAliasTest::RunTest(const FString& Parameters)
{
	FScopedIdentityTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeIdentityStorageFactory(Fixture.GetRootPath()), MakeIdentityTransportFactory(LastTransport), MakeIdentityUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeIdentityTestSettings(false);

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);

	const EPostHogCaptureResult Result = Controller.Alias(TEXT("alias-1"));
	TestEqual(TEXT("Alias succeeds"), Result, EPostHogCaptureResult::Success);

	Controller.Flush();
	if (!TestNotNull(TEXT("Transport created"), LastTransport))
	{
		return false;
	}

	TArray<TSharedPtr<FJsonObject>> Events;
	TestTrue(TEXT("Payload parsed"), TryGetIdentityPayloadEvents(LastTransport->GetLastPayload(), Events));
	LastTransport->CompleteLast(true, 200, TEXT(""));

	TestEqual(TEXT("One event queued"), Events.Num(), 1);
	if (Events.Num() != 1)
	{
		return false;
	}

	FString EventName;
	TestTrue(TEXT("Event has name"), Events[0]->TryGetStringField(TEXT("event"), EventName));
	TestEqual(TEXT("Event is $create_alias"), EventName, FString(TEXT("$create_alias")));

	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("Event has properties"), Events[0]->TryGetObjectField(TEXT("properties"), PropertiesObject));

	FString AliasValue;
	TestTrue(TEXT("properties has alias"), (*PropertiesObject)->TryGetStringField(TEXT("alias"), AliasValue));
	TestEqual(TEXT("alias value is correct"), AliasValue, FString(TEXT("alias-1")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerResetWithoutReuseChangesDistinctIdTest, "UnrealHog.Consent.ConsentController.ResetWithoutReuseAssignsNewAnonymousId", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerResetWithoutReuseChangesDistinctIdTest::RunTest(const FString& Parameters)
{
	FScopedIdentityTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeIdentityStorageFactory(Fixture.GetRootPath()), MakeIdentityTransportFactory(LastTransport), MakeIdentityUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeIdentityTestSettings(false);

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);
	Controller.Identify(TEXT("user-1"), nullptr, nullptr);
	TestEqual(TEXT("Identified before reset"), Controller.GetDistinctId(), FString(TEXT("user-1")));
	Controller.Flush();
	if (TestNotNull(TEXT("Transport created after identify"), LastTransport))
	{
		LastTransport->CompleteLast(true, 200, TEXT(""));
	}

	Controller.Reset(*Settings);

	const FString DistinctIdAfterReset = Controller.GetDistinctId();
	TestNotEqual(TEXT("Distinct id changes after reset without reuse"), DistinctIdAfterReset, FString(TEXT("user-1")));
	TestFalse(TEXT("New distinct id is non-empty"), DistinctIdAfterReset.IsEmpty());

	Controller.CaptureEvent(TEXT("post-reset-event"), nullptr);
	Controller.Flush();
	if (!TestNotNull(TEXT("Transport created"), LastTransport))
	{
		return false;
	}

	TArray<TSharedPtr<FJsonObject>> Events;
	TestTrue(TEXT("Payload parsed"), TryGetIdentityPayloadEvents(LastTransport->GetLastPayload(), Events));
	LastTransport->CompleteLast(true, 200, TEXT(""));

	TestEqual(TEXT("One event queued after reset"), Events.Num(), 1);
	if (Events.Num() == 1)
	{
		FString EventDistinctId;
		TestTrue(TEXT("Event has distinct_id"), Events[0]->TryGetStringField(TEXT("distinct_id"), EventDistinctId));
		TestEqual(TEXT("Subsequent event uses the new anonymous distinct id"), EventDistinctId, DistinctIdAfterReset);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerResetWithReuseKeepsDistinctIdTest, "UnrealHog.Consent.ConsentController.ResetWithReuseKeepsAnonymousId", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerResetWithReuseKeepsDistinctIdTest::RunTest(const FString& Parameters)
{
	FScopedIdentityTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeIdentityStorageFactory(Fixture.GetRootPath()), MakeIdentityTransportFactory(LastTransport), MakeIdentityUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeIdentityTestSettings(true);

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);
	const FString AnonymousIdBeforeIdentify = Controller.GetDistinctId();
	Controller.Identify(TEXT("user-1"), nullptr, nullptr);

	Controller.Reset(*Settings);

	TestEqual(TEXT("Distinct id reverts to the original anonymous id"), Controller.GetDistinctId(), AnonymousIdBeforeIdentify);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerResetRotatesSessionTest, "UnrealHog.Consent.ConsentController.ResetStartsNewSession", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerResetRotatesSessionTest::RunTest(const FString& Parameters)
{
	FScopedIdentityTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeIdentityStorageFactory(Fixture.GetRootPath()), MakeIdentityTransportFactory(LastTransport), MakeIdentityUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeIdentityTestSettings(false);

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);
	const FString SessionIdBeforeReset = Controller.GetSessionId();

	Controller.Reset(*Settings);

	TestNotEqual(TEXT("Session id rotates after reset"), Controller.GetSessionId(), SessionIdBeforeReset);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerResetRequiresConsentTest, "UnrealHog.Consent.ConsentController.ResetWithoutConsentIsNoOp", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerResetRequiresConsentTest::RunTest(const FString& Parameters)
{
	FScopedIdentityTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeIdentityStorageFactory(Fixture.GetRootPath()), MakeIdentityTransportFactory(LastTransport), MakeIdentityUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeIdentityTestSettings(false);

	Controller.Initialize(*Settings);

	// Should not crash or assert when not opted in.
	Controller.Reset(*Settings);

	TestTrue(TEXT("Distinct id remains empty without consent"), Controller.GetDistinctId().IsEmpty());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
