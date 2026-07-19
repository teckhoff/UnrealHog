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
	// these group tests; removed on scope exit even if an assertion fails mid-test.
	class FScopedGroupTestStorageDirectory
	{
	public:
		FScopedGroupTestStorageDirectory()
			: RootPath(FPaths::Combine(FPlatformProcess::UserTempDir(), TEXT("UnrealHogGroupTests"), FGuid::NewGuid().ToString(EGuidFormats::Digits)))
		{
		}

		~FScopedGroupTestStorageDirectory()
		{
			IFileManager::Get().DeleteDirectory(*RootPath, false, true);
		}

		const FString& GetRootPath() const { return RootPath; }

	private:
		FString RootPath;
	};

	UPostHogDeveloperSettings* MakeGroupTestSettings()
	{
		UPostHogDeveloperSettings* Settings = NewObject<UPostHogDeveloperSettings>(GetTransientPackage());
		UnrealHogTests::SetPropertyValue<FString>(Settings, TEXT("ApiKey"), TEXT("phc_valid_key"));
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bAnalyticsEnabled"), true);
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bDefaultUserOptIn"), false);
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bCaptureApplicationLifecycleEvents"), false);
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bReuseAnonymousId"), false);
		return Settings;
	}

	FPostHogConsentController::FStorageProviderFactory MakeGroupStorageFactory(const FString& RootPath)
	{
		return [RootPath]() -> TUniquePtr<IPostHogStorageProvider>
		{
			return MakeUnique<FPostHogFileStorageProvider>(RootPath);
		};
	}

	// Captures the most recently created fake transport so tests can drive its completion callbacks.
	FPostHogConsentController::FTransportFactory MakeGroupTransportFactory(FPostHogFakeBatchTransport*& OutLastTransport)
	{
		return [&OutLastTransport](const FString&) -> TUniquePtr<IPostHogBatchTransport>
		{
			TUniquePtr<FPostHogFakeBatchTransport> Transport = MakeUnique<FPostHogFakeBatchTransport>();
			OutLastTransport = Transport.Get();
			return Transport;
		};
	}

	// Deterministic, countable stand-in for PostHogUuidV7::New().
	FPostHogConsentController::FUuidGenerator MakeGroupUuidGenerator(int32& Counter)
	{
		return [&Counter]() { return FString::Printf(TEXT("group-uuid-%d"), ++Counter); };
	}

	bool TryGetGroupPayloadEvents(const FPostHogBatchPayload& Payload, TArray<TSharedPtr<FJsonObject>>& OutEvents)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerGroupBlankNoOpTest, "UnrealHog.Consent.ConsentController.GroupBlankTypeOrKeyIsNoOp", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerGroupBlankNoOpTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("rejected Group with an empty or whitespace-only group type or key"), EAutomationExpectedErrorFlags::Contains, 2, false);

	FScopedGroupTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeGroupStorageFactory(Fixture.GetRootPath()), MakeGroupTransportFactory(LastTransport), MakeGroupUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeGroupTestSettings();

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);

	const EPostHogCaptureResult BlankTypeResult = Controller.Group(TEXT("  "), TEXT("acme"), nullptr);
	const EPostHogCaptureResult BlankKeyResult = Controller.Group(TEXT("company"), TEXT(""), nullptr);

	TestEqual(TEXT("Blank group type rejected"), BlankTypeResult, EPostHogCaptureResult::InvalidEventName);
	TestEqual(TEXT("Blank group key rejected"), BlankKeyResult, EPostHogCaptureResult::InvalidEventName);
	TestEqual(TEXT("No events queued"), Controller.GetQueuedEventCount(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerGroupRequiresConsentTest, "UnrealHog.Consent.ConsentController.GroupRequiresConsent", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerGroupRequiresConsentTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("dropping Group for company"), EAutomationExpectedErrorFlags::Contains, 1, false);

	FScopedGroupTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeGroupStorageFactory(Fixture.GetRootPath()), MakeGroupTransportFactory(LastTransport), MakeGroupUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeGroupTestSettings();

	Controller.Initialize(*Settings);

	const EPostHogCaptureResult Result = Controller.Group(TEXT("company"), TEXT("acme"), nullptr);

	TestEqual(TEXT("Group without consent is rejected"), Result, EPostHogCaptureResult::NotOptedIn);
	TestEqual(TEXT("No events queued"), Controller.GetQueuedEventCount(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerGroupEmitsGroupIdentifyTest, "UnrealHog.Consent.ConsentController.GroupEmitsGroupIdentifyWithoutGroupSetWhenPropertiesOmitted", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerGroupEmitsGroupIdentifyTest::RunTest(const FString& Parameters)
{
	FScopedGroupTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeGroupStorageFactory(Fixture.GetRootPath()), MakeGroupTransportFactory(LastTransport), MakeGroupUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeGroupTestSettings();

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);

	const EPostHogCaptureResult Result = Controller.Group(TEXT("company"), TEXT("acme"), nullptr);
	TestEqual(TEXT("Group succeeds"), Result, EPostHogCaptureResult::Success);

	Controller.Flush();
	if (!TestNotNull(TEXT("Transport created"), LastTransport))
	{
		return false;
	}

	TArray<TSharedPtr<FJsonObject>> Events;
	TestTrue(TEXT("Payload parsed"), TryGetGroupPayloadEvents(LastTransport->GetLastPayload(), Events));
	LastTransport->CompleteLast(true, 200, TEXT(""));

	TestEqual(TEXT("One event queued"), Events.Num(), 1);
	if (Events.Num() != 1)
	{
		return false;
	}

	FString EventName;
	TestTrue(TEXT("Event has name"), Events[0]->TryGetStringField(TEXT("event"), EventName));
	TestEqual(TEXT("Event is $groupidentify"), EventName, FString(TEXT("$groupidentify")));

	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("Event has properties"), Events[0]->TryGetObjectField(TEXT("properties"), PropertiesObject));

	FString GroupType;
	TestTrue(TEXT("properties has $group_type"), (*PropertiesObject)->TryGetStringField(TEXT("$group_type"), GroupType));
	TestEqual(TEXT("$group_type is correct"), GroupType, FString(TEXT("company")));

	FString GroupKey;
	TestTrue(TEXT("properties has $group_key"), (*PropertiesObject)->TryGetStringField(TEXT("$group_key"), GroupKey));
	TestEqual(TEXT("$group_key is correct"), GroupKey, FString(TEXT("acme")));

	TestFalse(TEXT("$group_set absent when properties omitted"), (*PropertiesObject)->HasField(TEXT("$group_set")));

	const TSharedPtr<FJsonObject>* GroupsObject = nullptr;
	TestTrue(TEXT("properties has $groups"), (*PropertiesObject)->TryGetObjectField(TEXT("$groups"), GroupsObject));
	FString GroupsCompanyValue;
	TestTrue(TEXT("$groups has company"), (*GroupsObject)->TryGetStringField(TEXT("company"), GroupsCompanyValue));
	TestEqual(TEXT("$groups.company is correct"), GroupsCompanyValue, FString(TEXT("acme")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerGroupWithPropertiesNestsGroupSetAndDeepCopiesTest, "UnrealHog.Consent.ConsentController.GroupWithPropertiesNestsGroupSetAndDeepCopies", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerGroupWithPropertiesNestsGroupSetAndDeepCopiesTest::RunTest(const FString& Parameters)
{
	FScopedGroupTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeGroupStorageFactory(Fixture.GetRootPath()), MakeGroupTransportFactory(LastTransport), MakeGroupUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeGroupTestSettings();

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);

	UPostHogEventProperties* GroupProperties = NewObject<UPostHogEventProperties>();
	GroupProperties->AddString(TEXT("plan"), TEXT("enterprise"));

	const EPostHogCaptureResult Result = Controller.Group(TEXT("company"), TEXT("acme"), GroupProperties);
	TestEqual(TEXT("Group succeeds"), Result, EPostHogCaptureResult::Success);

	// Mutate the properties object after capture; the already-captured event must not change.
	GroupProperties->AddString(TEXT("plan"), TEXT("mutated-after-capture"));
	GroupProperties->AddString(TEXT("extra"), TEXT("should-not-appear"));

	Controller.Flush();
	if (!TestNotNull(TEXT("Transport created"), LastTransport))
	{
		return false;
	}

	TArray<TSharedPtr<FJsonObject>> Events;
	TestTrue(TEXT("Payload parsed"), TryGetGroupPayloadEvents(LastTransport->GetLastPayload(), Events));
	LastTransport->CompleteLast(true, 200, TEXT(""));

	TestEqual(TEXT("One event queued"), Events.Num(), 1);
	if (Events.Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("Event has properties"), Events[0]->TryGetObjectField(TEXT("properties"), PropertiesObject));

	const TSharedPtr<FJsonObject>* GroupSetObject = nullptr;
	TestTrue(TEXT("properties has $group_set"), (*PropertiesObject)->TryGetObjectField(TEXT("$group_set"), GroupSetObject));

	FString PlanValue;
	TestTrue(TEXT("$group_set has plan"), (*GroupSetObject)->TryGetStringField(TEXT("plan"), PlanValue));
	TestEqual(TEXT("$group_set.plan is the pre-mutation value"), PlanValue, FString(TEXT("enterprise")));
	TestFalse(TEXT("$group_set does not have post-capture 'extra' field"), (*GroupSetObject)->HasField(TEXT("extra")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerGroupUpdatingOneTypeRetainsOthersInGroupsTest, "UnrealHog.Consent.ConsentController.GroupUpdatingOneTypeRetainsOthersInSubsequentGroups", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerGroupUpdatingOneTypeRetainsOthersInGroupsTest::RunTest(const FString& Parameters)
{
	FScopedGroupTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeGroupStorageFactory(Fixture.GetRootPath()), MakeGroupTransportFactory(LastTransport), MakeGroupUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeGroupTestSettings();

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);

	Controller.Group(TEXT("company"), TEXT("acme"), nullptr);
	Controller.Flush();
	if (TestNotNull(TEXT("Transport created after first group"), LastTransport))
	{
		LastTransport->CompleteLast(true, 200, TEXT(""));
	}

	const EPostHogCaptureResult SecondResult = Controller.Group(TEXT("team"), TEXT("eng"), nullptr);
	TestEqual(TEXT("Second group succeeds"), SecondResult, EPostHogCaptureResult::Success);

	Controller.Flush();
	if (!TestNotNull(TEXT("Transport created after second group"), LastTransport))
	{
		return false;
	}

	TArray<TSharedPtr<FJsonObject>> Events;
	TestTrue(TEXT("Payload parsed"), TryGetGroupPayloadEvents(LastTransport->GetLastPayload(), Events));
	LastTransport->CompleteLast(true, 200, TEXT(""));

	TestEqual(TEXT("One event queued in second flush"), Events.Num(), 1);
	if (Events.Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("Event has properties"), Events[0]->TryGetObjectField(TEXT("properties"), PropertiesObject));

	const TSharedPtr<FJsonObject>* GroupsObject = nullptr;
	TestTrue(TEXT("properties has $groups"), (*PropertiesObject)->TryGetObjectField(TEXT("$groups"), GroupsObject));

	FString CompanyValue;
	TestTrue(TEXT("$groups retains company from first call"), (*GroupsObject)->TryGetStringField(TEXT("company"), CompanyValue));
	TestEqual(TEXT("$groups.company unchanged"), CompanyValue, FString(TEXT("acme")));

	FString TeamValue;
	TestTrue(TEXT("$groups has new team membership"), (*GroupsObject)->TryGetStringField(TEXT("team"), TeamValue));
	TestEqual(TEXT("$groups.team is correct"), TeamValue, FString(TEXT("eng")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerNormalEventCarriesGroupsTest, "UnrealHog.Consent.ConsentController.NormalCaptureEventCarriesGroupsAfterGroupCall", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerNormalEventCarriesGroupsTest::RunTest(const FString& Parameters)
{
	FScopedGroupTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeGroupStorageFactory(Fixture.GetRootPath()), MakeGroupTransportFactory(LastTransport), MakeGroupUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeGroupTestSettings();

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);

	Controller.Group(TEXT("company"), TEXT("acme"), nullptr);
	Controller.Flush();
	if (TestNotNull(TEXT("Transport created after group"), LastTransport))
	{
		LastTransport->CompleteLast(true, 200, TEXT(""));
	}

	const EPostHogCaptureResult Result = Controller.CaptureEvent(TEXT("normal-event"), nullptr);
	TestEqual(TEXT("Normal capture succeeds"), Result, EPostHogCaptureResult::Success);

	Controller.Flush();
	if (!TestNotNull(TEXT("Transport created"), LastTransport))
	{
		return false;
	}

	TArray<TSharedPtr<FJsonObject>> Events;
	TestTrue(TEXT("Payload parsed"), TryGetGroupPayloadEvents(LastTransport->GetLastPayload(), Events));
	LastTransport->CompleteLast(true, 200, TEXT(""));

	TestEqual(TEXT("One event queued"), Events.Num(), 1);
	if (Events.Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("Event has properties"), Events[0]->TryGetObjectField(TEXT("properties"), PropertiesObject));

	const TSharedPtr<FJsonObject>* GroupsObject = nullptr;
	TestTrue(TEXT("properties has $groups"), (*PropertiesObject)->TryGetObjectField(TEXT("$groups"), GroupsObject));
	FString CompanyValue;
	TestTrue(TEXT("$groups has company"), (*GroupsObject)->TryGetStringField(TEXT("company"), CompanyValue));
	TestEqual(TEXT("$groups.company is correct"), CompanyValue, FString(TEXT("acme")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerResetGroupsClearsSubsequentEventGroupsTest, "UnrealHog.Consent.ConsentController.ResetGroupsClearsSubsequentEventGroups", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerResetGroupsClearsSubsequentEventGroupsTest::RunTest(const FString& Parameters)
{
	FScopedGroupTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeGroupStorageFactory(Fixture.GetRootPath()), MakeGroupTransportFactory(LastTransport), MakeGroupUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeGroupTestSettings();

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);

	Controller.Group(TEXT("company"), TEXT("acme"), nullptr);
	Controller.Flush();
	if (TestNotNull(TEXT("Transport created after group"), LastTransport))
	{
		LastTransport->CompleteLast(true, 200, TEXT(""));
	}

	const int32 QueuedCountBeforeResetGroups = Controller.GetQueuedEventCount();
	Controller.ResetGroups();
	TestEqual(TEXT("ResetGroups enqueues no event"), Controller.GetQueuedEventCount(), QueuedCountBeforeResetGroups);

	Controller.CaptureEvent(TEXT("post-reset-groups-event"), nullptr);
	Controller.Flush();
	if (!TestNotNull(TEXT("Transport created"), LastTransport))
	{
		return false;
	}

	TArray<TSharedPtr<FJsonObject>> Events;
	TestTrue(TEXT("Payload parsed"), TryGetGroupPayloadEvents(LastTransport->GetLastPayload(), Events));
	LastTransport->CompleteLast(true, 200, TEXT(""));

	TestEqual(TEXT("One event queued"), Events.Num(), 1);
	if (Events.Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("Event has properties"), Events[0]->TryGetObjectField(TEXT("properties"), PropertiesObject));
	TestFalse(TEXT("$groups absent after ResetGroups"), (*PropertiesObject)->HasField(TEXT("$groups")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerCallerCannotOverrideGroupsTest, "UnrealHog.Consent.ConsentController.CallerCallPropertiesCannotOverrideGroups", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerCallerCannotOverrideGroupsTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("protected PostHog property \"$groups\""), EAutomationExpectedErrorFlags::Contains, 1, false);

	FScopedGroupTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeGroupStorageFactory(Fixture.GetRootPath()), MakeGroupTransportFactory(LastTransport), MakeGroupUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeGroupTestSettings();

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);

	Controller.Group(TEXT("company"), TEXT("acme"), nullptr);
	Controller.Flush();
	if (TestNotNull(TEXT("Transport created after group"), LastTransport))
	{
		LastTransport->CompleteLast(true, 200, TEXT(""));
	}

	UPostHogEventProperties* CallerProperties = NewObject<UPostHogEventProperties>();
	UPostHogEventProperties* SpoofedGroups = NewObject<UPostHogEventProperties>();
	SpoofedGroups->AddString(TEXT("company"), TEXT("attacker-controlled"));
	CallerProperties->AddObject(TEXT("$groups"), SpoofedGroups);

	const EPostHogCaptureResult Result = Controller.CaptureEvent(TEXT("normal-event"), CallerProperties);
	TestEqual(TEXT("Capture succeeds"), Result, EPostHogCaptureResult::Success);

	Controller.Flush();
	if (!TestNotNull(TEXT("Transport created"), LastTransport))
	{
		return false;
	}

	TArray<TSharedPtr<FJsonObject>> Events;
	TestTrue(TEXT("Payload parsed"), TryGetGroupPayloadEvents(LastTransport->GetLastPayload(), Events));
	LastTransport->CompleteLast(true, 200, TEXT(""));

	TestEqual(TEXT("One event queued"), Events.Num(), 1);
	if (Events.Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("Event has properties"), Events[0]->TryGetObjectField(TEXT("properties"), PropertiesObject));

	const TSharedPtr<FJsonObject>* GroupsObject = nullptr;
	TestTrue(TEXT("properties has $groups"), (*PropertiesObject)->TryGetObjectField(TEXT("$groups"), GroupsObject));
	FString CompanyValue;
	TestTrue(TEXT("$groups has company"), (*GroupsObject)->TryGetStringField(TEXT("company"), CompanyValue));
	TestEqual(TEXT("SDK-owned $groups wins over caller-supplied value"), CompanyValue, FString(TEXT("acme")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
