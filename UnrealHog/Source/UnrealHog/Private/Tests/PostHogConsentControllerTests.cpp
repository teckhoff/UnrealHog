#include "Consent/PostHogConsentController.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Events/PostHogBatchPayload.h"
#include "Events/PostHogBeforeSend.h"
#include "Events/PostHogEvent.h"
#include "Events/PostHogEventProperties.h"
#include "Events/PostHogEventQueue.h"
#include "PostHogDeveloperSettings.h"
#include "SDK/PostHogSdkInfo.h"
#include "Storage/PostHogFileStorageProvider.h"
#include "Tests/PostHogFakeBatchTransport.h"
#include "Tests/PostHogTestPropertyHelpers.h"
#include "UObject/Package.h"

namespace
{
	// RAII fixture that owns a unique temporary directory for the file storage provider backing
	// these consent controller tests; removed on scope exit even if an assertion fails mid-test.
	class FScopedConsentTestStorageDirectory
	{
	public:
		FScopedConsentTestStorageDirectory()
			: RootPath(FPaths::Combine(FPlatformProcess::UserTempDir(), TEXT("UnrealHogTests"), FGuid::NewGuid().ToString(EGuidFormats::Digits)))
		{
		}

		~FScopedConsentTestStorageDirectory()
		{
			IFileManager::Get().DeleteDirectory(*RootPath, false, true);
		}

		const FString& GetRootPath() const { return RootPath; }

		FString GetQueueDirectory() const
		{
			return FPaths::Combine(RootPath, FPostHogSdkInfo::GetLibraryName(), TEXT("Queue"));
		}

		FString GetStateDirectory() const
		{
			return FPaths::Combine(RootPath, FPostHogSdkInfo::GetLibraryName(), TEXT("State"));
		}

	private:
		FString RootPath;
	};

	UPostHogDeveloperSettings* MakeTransientSettings(bool bValidApiKey, bool bAnalyticsEnabled, bool bDefaultUserOptIn)
	{
		UPostHogDeveloperSettings* Settings = NewObject<UPostHogDeveloperSettings>(GetTransientPackage());
		UnrealHogTests::SetPropertyValue<FString>(Settings, TEXT("ApiKey"), bValidApiKey ? TEXT("phc_valid_key") : TEXT(""));
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bAnalyticsEnabled"), bAnalyticsEnabled);
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bDefaultUserOptIn"), bDefaultUserOptIn);
		return Settings;
	}

	FPostHogConsentController::FStorageProviderFactory MakeStorageFactory(const FString& RootPath)
	{
		return [RootPath]() -> TUniquePtr<IPostHogStorageProvider>
		{
			return MakeUnique<FPostHogFileStorageProvider>(RootPath);
		};
	}

	// Captures the most recently created fake transport so tests can drive its completion callbacks.
	FPostHogConsentController::FTransportFactory MakeTransportFactory(FPostHogFakeBatchTransport*& OutLastTransport)
	{
		return [&OutLastTransport](const FString&) -> TUniquePtr<IPostHogBatchTransport>
		{
			TUniquePtr<FPostHogFakeBatchTransport> Transport = MakeUnique<FPostHogFakeBatchTransport>();
			OutLastTransport = Transport.Get();
			return Transport;
		};
	}

	// Deterministic, countable stand-in for PostHogUuidV7::New().
	FPostHogConsentController::FUuidGenerator MakeUuidGenerator(int32& Counter)
	{
		return [&Counter]() { return FString::Printf(TEXT("session-%d"), ++Counter); };
	}

	FPostHogEvent MakeConsentTestEvent(const FString& Suffix)
	{
		return FPostHogEvent(FString::Printf(TEXT("test-event-%s"), *Suffix), TEXT("distinct-id"));
	}

	bool TryGetSinglePayloadEvent(const FPostHogBatchPayload& Payload, TSharedPtr<FJsonObject>& OutEventObject)
	{
		const TSharedRef<FJsonObject> PayloadJson = Payload.ToJsonObject();

		const TArray<TSharedPtr<FJsonValue>>* BatchArray = nullptr;
		if (!PayloadJson->TryGetArrayField(TEXT("batch"), BatchArray) || BatchArray->Num() != 1)
		{
			return false;
		}

		OutEventObject = (*BatchArray)[0]->AsObject();
		return OutEventObject.IsValid();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerDefaultOptOutTest, "UnrealHog.Consent.ConsentController.DefaultOptOutInitializeIsSideEffectFree", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerDefaultOptOutTest::RunTest(const FString& Parameters)
{
	FScopedConsentTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeStorageFactory(Fixture.GetRootPath()), MakeTransportFactory(LastTransport), MakeUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeTransientSettings(true, true, false);

	Controller.Initialize(*Settings);

	TestFalse(TEXT("Not opted in by default"), Controller.IsOptedIn());
	TestTrue(TEXT("No session id created"), Controller.GetSessionId().IsEmpty());
	TestEqual(TEXT("No transport created"), Controller.GetTransportCreationCount(), 0);
	TestEqual(TEXT("No session created"), Controller.GetSessionCreationCount(), 0);
	TestFalse(TEXT("Capture is a no-op before consent"), Controller.Capture(MakeConsentTestEvent(TEXT("1"))));
	TestEqual(TEXT("No events queued"), Controller.GetQueuedEventCount(), 0);
	TestFalse(TEXT("No queue directory created before consent"), IFileManager::Get().DirectoryExists(*Fixture.GetQueueDirectory()));
	TestFalse(TEXT("No state directory created before consent"), IFileManager::Get().DirectoryExists(*Fixture.GetStateDirectory()));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerOptInIdempotentTest, "UnrealHog.Consent.ConsentController.OptInWithValidSettingsEnablesCaptureIdempotently", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerOptInIdempotentTest::RunTest(const FString& Parameters)
{
	FScopedConsentTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeStorageFactory(Fixture.GetRootPath()), MakeTransportFactory(LastTransport), MakeUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeTransientSettings(true, true, false);

	Controller.Initialize(*Settings);

	TestTrue(TEXT("First opt-in succeeds"), Controller.SetOptIn(true, *Settings));
	TestEqual(TEXT("One transport created"), Controller.GetTransportCreationCount(), 1);
	TestEqual(TEXT("One session created"), Controller.GetSessionCreationCount(), 1);
	TestFalse(TEXT("Session id assigned"), Controller.GetSessionId().IsEmpty());

	TestTrue(TEXT("Repeat opt-in succeeds"), Controller.SetOptIn(true, *Settings));
	TestEqual(TEXT("Repeat opt-in does not create another transport"), Controller.GetTransportCreationCount(), 1);
	TestEqual(TEXT("Repeat opt-in does not create another session"), Controller.GetSessionCreationCount(), 1);

	TestTrue(TEXT("Capture succeeds once opted in"), Controller.Capture(MakeConsentTestEvent(TEXT("1"))));
	TestEqual(TEXT("Event queued"), Controller.GetQueuedEventCount(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerOptOutClearsQueueTest, "UnrealHog.Consent.ConsentController.OptOutClearsQueueAndBlocksCapture", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerOptOutClearsQueueTest::RunTest(const FString& Parameters)
{
	FScopedConsentTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeStorageFactory(Fixture.GetRootPath()), MakeTransportFactory(LastTransport), MakeUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeTransientSettings(true, true, false);

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);

	Controller.Capture(MakeConsentTestEvent(TEXT("1")));
	Controller.Capture(MakeConsentTestEvent(TEXT("2")));
	TestEqual(TEXT("Two events queued before opt-out"), Controller.GetQueuedEventCount(), 2);

	TestTrue(TEXT("Opt-out succeeds"), Controller.SetOptIn(false, *Settings));

	TestFalse(TEXT("No longer opted in"), Controller.IsOptedIn());
	TestEqual(TEXT("Queue cleared on opt-out"), Controller.GetQueuedEventCount(), 0);
	TestNull(TEXT("Event queue released"), Controller.GetEventQueue());
	TestFalse(TEXT("Capture blocked after opt-out"), Controller.Capture(MakeConsentTestEvent(TEXT("3"))));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerReOptInFreshSessionTest, "UnrealHog.Consent.ConsentController.ReOptInAfterOptOutCreatesFreshSessionWithoutDuplication", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerReOptInFreshSessionTest::RunTest(const FString& Parameters)
{
	FScopedConsentTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeStorageFactory(Fixture.GetRootPath()), MakeTransportFactory(LastTransport), MakeUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeTransientSettings(true, true, false);

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);
	TestEqual(TEXT("One session created initially"), Controller.GetSessionCreationCount(), 1);
	const FString FirstSessionId = Controller.GetSessionId();

	Controller.SetOptIn(false, *Settings);
	Controller.SetOptIn(true, *Settings);

	TestEqual(TEXT("Second session created on re-opt-in"), Controller.GetSessionCreationCount(), 2);
	TestEqual(TEXT("Second transport created on re-opt-in"), Controller.GetTransportCreationCount(), 2);
	TestNotEqual(TEXT("Fresh session id differs from the original"), Controller.GetSessionId(), FirstSessionId);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerInvalidConfigRejectsTest, "UnrealHog.Consent.ConsentController.InvalidConfigurationRejectsOptIn", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerInvalidConfigRejectsTest::RunTest(const FString& Parameters)
{
	FScopedConsentTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeStorageFactory(Fixture.GetRootPath()), MakeTransportFactory(LastTransport), MakeUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeTransientSettings(false, true, false);

	Controller.Initialize(*Settings);

	TestFalse(TEXT("Opt-in rejected with empty API key"), Controller.SetOptIn(true, *Settings));
	TestFalse(TEXT("Remains opted out"), Controller.IsOptedIn());
	TestEqual(TEXT("No transport created"), Controller.GetTransportCreationCount(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerKillSwitchBlocksTest, "UnrealHog.Consent.ConsentController.AnalyticsDisabledKillSwitchBlocksOptIn", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerKillSwitchBlocksTest::RunTest(const FString& Parameters)
{
	FScopedConsentTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeStorageFactory(Fixture.GetRootPath()), MakeTransportFactory(LastTransport), MakeUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeTransientSettings(true, false, false);

	Controller.Initialize(*Settings);

	TestFalse(TEXT("Opt-in rejected while analytics kill switch is off"), Controller.SetOptIn(true, *Settings));
	TestFalse(TEXT("Remains opted out"), Controller.IsOptedIn());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerPersistsAcrossReinitializeTest, "UnrealHog.Consent.ConsentController.OptInStatePersistsAcrossReinitialize", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerPersistsAcrossReinitializeTest::RunTest(const FString& Parameters)
{
	FScopedConsentTestStorageDirectory Fixture;
	UPostHogDeveloperSettings* Settings = MakeTransientSettings(true, true, false);

	{
		FPostHogFakeBatchTransport* LastTransportA = nullptr;
		int32 UuidCounterA = 0;
		FPostHogConsentController ControllerA(MakeStorageFactory(Fixture.GetRootPath()), MakeTransportFactory(LastTransportA), MakeUuidGenerator(UuidCounterA));

		ControllerA.Initialize(*Settings);
		ControllerA.SetOptIn(true, *Settings);
	}

	{
		FPostHogFakeBatchTransport* LastTransportB = nullptr;
		int32 UuidCounterB = 0;
		FPostHogConsentController ControllerB(MakeStorageFactory(Fixture.GetRootPath()), MakeTransportFactory(LastTransportB), MakeUuidGenerator(UuidCounterB));

		ControllerB.Initialize(*Settings);

		TestTrue(TEXT("Persisted opt-in takes precedence over settings default"), ControllerB.IsOptedIn());
		TestEqual(TEXT("Restoring collaborators creates exactly one transport"), ControllerB.GetTransportCreationCount(), 1);

		ControllerB.SetOptIn(false, *Settings);
	}

	{
		FPostHogFakeBatchTransport* LastTransportC = nullptr;
		int32 UuidCounterC = 0;
		FPostHogConsentController ControllerC(MakeStorageFactory(Fixture.GetRootPath()), MakeTransportFactory(LastTransportC), MakeUuidGenerator(UuidCounterC));

		ControllerC.Initialize(*Settings);

		TestFalse(TEXT("Persisted opt-out is honored"), ControllerC.IsOptedIn());
		TestEqual(TEXT("No transport created when opted out"), ControllerC.GetTransportCreationCount(), 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerCaptureEventRejectsInvalidNameTest, "UnrealHog.Consent.ConsentController.CaptureEventRejectsInvalidName", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerCaptureEventRejectsInvalidNameTest::RunTest(const FString& Parameters)
{
	FScopedConsentTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeStorageFactory(Fixture.GetRootPath()), MakeTransportFactory(LastTransport), MakeUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeTransientSettings(true, true, false);

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);

	const EPostHogCaptureResult Result = Controller.CaptureEvent(TEXT("   "), nullptr, false);

	TestEqual(TEXT("Whitespace-only name is rejected"), Result, EPostHogCaptureResult::InvalidEventName);
	TestEqual(TEXT("No event queued"), Controller.GetQueuedEventCount(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerCaptureEventRejectsWithoutConsentTest, "UnrealHog.Consent.ConsentController.CaptureEventRejectsWithoutConsent", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerCaptureEventRejectsWithoutConsentTest::RunTest(const FString& Parameters)
{
	FScopedConsentTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeStorageFactory(Fixture.GetRootPath()), MakeTransportFactory(LastTransport), MakeUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeTransientSettings(true, true, false);

	Controller.Initialize(*Settings);

	const EPostHogCaptureResult Result = Controller.CaptureEvent(TEXT("ev"), nullptr, false);

	TestEqual(TEXT("Capture without consent is rejected"), Result, EPostHogCaptureResult::NotOptedIn);
	TestEqual(TEXT("No event queued"), Controller.GetQueuedEventCount(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerCaptureEventNullPropertiesRetainsSdkEnrichmentTest, "UnrealHog.Consent.ConsentController.CaptureEventNullPropertiesRetainsSdkEnrichment", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerCaptureEventNullPropertiesRetainsSdkEnrichmentTest::RunTest(const FString& Parameters)
{
	FScopedConsentTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeStorageFactory(Fixture.GetRootPath()), MakeTransportFactory(LastTransport), MakeUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeTransientSettings(true, true, false);

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);

	const EPostHogCaptureResult Result = Controller.CaptureEvent(TEXT("ev"), nullptr, false);

	TestEqual(TEXT("Capture with null properties succeeds"), Result, EPostHogCaptureResult::Success);
	TestEqual(TEXT("One event queued"), Controller.GetQueuedEventCount(), 1);

	Controller.Flush();
	TestNotNull(TEXT("Transport created"), LastTransport);

	const TSharedRef<FJsonObject> PayloadJson = LastTransport->GetLastPayload().ToJsonObject();
	LastTransport->CompleteLast(true, 200, TEXT(""));

	const TArray<TSharedPtr<FJsonValue>>* BatchArray = nullptr;
	TestTrue(TEXT("Has batch array"), PayloadJson->TryGetArrayField(TEXT("batch"), BatchArray));
	TestEqual(TEXT("Batch has one event"), BatchArray->Num(), 1);

	const TSharedPtr<FJsonObject> EventObject = (*BatchArray)[0]->AsObject();
	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("Event has properties object"), EventObject->TryGetObjectField(TEXT("properties"), PropertiesObject));

	FString LibValue;
	TestTrue(TEXT("properties has $lib"), (*PropertiesObject)->TryGetStringField(TEXT("$lib"), LibValue));
	TestFalse(TEXT("$lib is non-empty"), LibValue.IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerCaptureEventEnforcesReservedPropertyPrecedenceTest, "UnrealHog.Consent.ConsentController.CaptureEventEnforcesReservedPropertyPrecedence", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerCaptureEventEnforcesReservedPropertyPrecedenceTest::RunTest(const FString& Parameters)
{
	FScopedConsentTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeStorageFactory(Fixture.GetRootPath()), MakeTransportFactory(LastTransport), MakeUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeTransientSettings(true, true, false);

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);

	const FString ExpectedSessionId = Controller.GetSessionId();

	UPostHogEventProperties* Properties = NewObject<UPostHogEventProperties>();
	Properties->AddString(TEXT("$lib"), TEXT("attacker"));
	Properties->AddString(TEXT("$lib_version"), TEXT("attacker"));
	Properties->AddBoolean(TEXT("$process_person_profile"), true);
	Properties->AddString(TEXT("$session_id"), TEXT("attacker-session"));
	Properties->AddString(TEXT("$groups"), TEXT("attacker-groups"));

	const EPostHogCaptureResult Result = Controller.CaptureEvent(TEXT("ev"), Properties, false);
	TestEqual(TEXT("Capture succeeds"), Result, EPostHogCaptureResult::Success);

	Controller.Flush();
	TestNotNull(TEXT("Transport created"), LastTransport);

	const TSharedRef<FJsonObject> PayloadJson = LastTransport->GetLastPayload().ToJsonObject();
	LastTransport->CompleteLast(true, 200, TEXT(""));

	const TArray<TSharedPtr<FJsonValue>>* BatchArray = nullptr;
	TestTrue(TEXT("Has batch array"), PayloadJson->TryGetArrayField(TEXT("batch"), BatchArray));
	TestEqual(TEXT("Batch has one event"), BatchArray->Num(), 1);

	const TSharedPtr<FJsonObject> EventObject = (*BatchArray)[0]->AsObject();
	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("Event has properties object"), EventObject->TryGetObjectField(TEXT("properties"), PropertiesObject));

	FString LibValue;
	TestTrue(TEXT("properties has $lib"), (*PropertiesObject)->TryGetStringField(TEXT("$lib"), LibValue));
	TestEqual(TEXT("$lib is the SDK library name, not the caller's value"), LibValue, FPostHogSdkInfo::GetLibraryName());

	FString LibVersionValue;
	TestTrue(TEXT("properties has $lib_version"), (*PropertiesObject)->TryGetStringField(TEXT("$lib_version"), LibVersionValue));
	TestEqual(TEXT("$lib_version is the SDK plugin version, not the caller's value"), LibVersionValue, FPostHogSdkInfo::GetPluginVersion());

	bool bProcessPersonProfileValue = true;
	TestTrue(TEXT("properties has $process_person_profile"), (*PropertiesObject)->TryGetBoolField(TEXT("$process_person_profile"), bProcessPersonProfileValue));
	TestFalse(TEXT("$process_person_profile is the SDK-supplied value, not the caller's true"), bProcessPersonProfileValue);

	FString SessionIdValue;
	TestTrue(TEXT("properties has $session_id"), (*PropertiesObject)->TryGetStringField(TEXT("$session_id"), SessionIdValue));
	TestEqual(TEXT("$session_id is the controller's session id, not the caller's value"), SessionIdValue, ExpectedSessionId);

	TestFalse(TEXT("$groups from caller input never survives (no group source exists yet)"), (*PropertiesObject)->HasField(TEXT("$groups")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerCaptureEventDuplicateCallerKeyLastWriteWinsTest, "UnrealHog.Consent.ConsentController.CaptureEventDuplicateCallerKeyLastWriteWins", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerCaptureEventDuplicateCallerKeyLastWriteWinsTest::RunTest(const FString& Parameters)
{
	FScopedConsentTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeStorageFactory(Fixture.GetRootPath()), MakeTransportFactory(LastTransport), MakeUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeTransientSettings(true, true, false);

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);

	UPostHogEventProperties* Properties = NewObject<UPostHogEventProperties>();
	Properties->AddString(TEXT("dup"), TEXT("first"));
	Properties->AddString(TEXT("dup"), TEXT("second"));

	const EPostHogCaptureResult Result = Controller.CaptureEvent(TEXT("ev"), Properties, false);
	TestEqual(TEXT("Capture succeeds"), Result, EPostHogCaptureResult::Success);

	Controller.Flush();
	TestNotNull(TEXT("Transport created"), LastTransport);

	const TSharedRef<FJsonObject> PayloadJson = LastTransport->GetLastPayload().ToJsonObject();
	LastTransport->CompleteLast(true, 200, TEXT(""));

	const TArray<TSharedPtr<FJsonValue>>* BatchArray = nullptr;
	TestTrue(TEXT("Has batch array"), PayloadJson->TryGetArrayField(TEXT("batch"), BatchArray));
	TestEqual(TEXT("Batch has one event"), BatchArray->Num(), 1);

	const TSharedPtr<FJsonObject> EventObject = (*BatchArray)[0]->AsObject();
	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("Event has properties object"), EventObject->TryGetObjectField(TEXT("properties"), PropertiesObject));

	FString DupValue;
	TestTrue(TEXT("properties has dup"), (*PropertiesObject)->TryGetStringField(TEXT("dup"), DupValue));
	TestEqual(TEXT("Last-added duplicate key value wins"), DupValue, TEXT("second"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerBeforeSendMutateTest, "UnrealHog.Consent.ConsentController.BeforeSendCanInspectAndMutateFinalEvent", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerBeforeSendMutateTest::RunTest(const FString& Parameters)
{
	FScopedConsentTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeStorageFactory(Fixture.GetRootPath()), MakeTransportFactory(LastTransport), MakeUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeTransientSettings(true, true, false);

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);

	const FString ExpectedSessionId = Controller.GetSessionId();

	UPostHogEventProperties* Properties = NewObject<UPostHogEventProperties>();
	Properties->AddString(TEXT("source"), TEXT("caller"));
	Properties->AddString(TEXT("secret"), TEXT("remove"));

	bool bHookCalled = false;
	bool bSawLibrary = false;
	bool bSawSession = false;
	bool bSawCallerProperty = false;
	bool bSawSecretProperty = false;
	FString SeenEventName;
	FString SeenDistinctId;
	FString SeenEventUuid;
	FString SeenTimestamp;

	FPostHogBeforeSendDelegate BeforeSend;
	BeforeSend.BindLambda([&](FPostHogBeforeSendEvent& Event)
	{
		bHookCalled = true;
		SeenEventName = Event.GetEventName();
		SeenDistinctId = Event.GetDistinctId();
		SeenEventUuid = Event.GetEventUuid();
		SeenTimestamp = Event.GetTimestamp();

		const FJsonObject& ReadOnlyProperties = Event.GetProperties();

		FString LibraryName;
		bSawLibrary = ReadOnlyProperties.TryGetStringField(TEXT("$lib"), LibraryName)
			&& LibraryName == FPostHogSdkInfo::GetLibraryName();

		FString SessionId;
		bSawSession = ReadOnlyProperties.TryGetStringField(TEXT("$session_id"), SessionId)
			&& SessionId == ExpectedSessionId;

		FString SourceValue;
		bSawCallerProperty = ReadOnlyProperties.TryGetStringField(TEXT("source"), SourceValue)
			&& SourceValue == TEXT("caller");

		FString SecretValue;
		bSawSecretProperty = ReadOnlyProperties.TryGetStringField(TEXT("secret"), SecretValue)
			&& SecretValue == TEXT("remove");

		FJsonObject& MutableProperties = Event.GetMutableProperties();
		MutableProperties.RemoveField(TEXT("secret"));
		MutableProperties.SetBoolField(TEXT("before_send"), true);

		return EPostHogBeforeSendResult::Continue;
	});
	Controller.SetBeforeSend(MoveTemp(BeforeSend));

	const EPostHogCaptureResult Result = Controller.CaptureEvent(TEXT("before-send-event"), Properties, false);

	TestEqual(TEXT("Capture succeeds"), Result, EPostHogCaptureResult::Success);
	TestTrue(TEXT("Before-send hook was invoked"), bHookCalled);
	TestEqual(TEXT("Hook sees event name"), SeenEventName, TEXT("before-send-event"));
	TestEqual(TEXT("Hook sees distinct id"), SeenDistinctId, ExpectedSessionId);
	TestFalse(TEXT("Hook sees event uuid"), SeenEventUuid.IsEmpty());
	TestFalse(TEXT("Hook sees timestamp"), SeenTimestamp.IsEmpty());
	TestTrue(TEXT("Hook sees final SDK library property"), bSawLibrary);
	TestTrue(TEXT("Hook sees final session property"), bSawSession);
	TestTrue(TEXT("Hook sees caller property"), bSawCallerProperty);
	TestTrue(TEXT("Hook sees removable property"), bSawSecretProperty);
	TestEqual(TEXT("One event queued"), Controller.GetQueuedEventCount(), 1);

	Controller.Flush();
	if (!TestNotNull(TEXT("Transport created"), LastTransport))
	{
		return false;
	}
	TestEqual(TEXT("One batch sent"), LastTransport->GetSentCount(), 1);

	TSharedPtr<FJsonObject> EventObject;
	if (!TestTrue(TEXT("Payload contains one event"), TryGetSinglePayloadEvent(LastTransport->GetLastPayload(), EventObject)))
	{
		return false;
	}
	LastTransport->CompleteLast(true, 200, TEXT(""));

	FString PersistedEventName;
	TestTrue(TEXT("Persisted event has name"), EventObject->TryGetStringField(TEXT("event"), PersistedEventName));
	TestEqual(TEXT("Persisted event name matches hook view"), PersistedEventName, SeenEventName);

	FString PersistedDistinctId;
	TestTrue(TEXT("Persisted event has distinct id"), EventObject->TryGetStringField(TEXT("distinct_id"), PersistedDistinctId));
	TestEqual(TEXT("Persisted distinct id matches hook view"), PersistedDistinctId, SeenDistinctId);

	FString PersistedUuid;
	TestTrue(TEXT("Persisted event has uuid"), EventObject->TryGetStringField(TEXT("uuid"), PersistedUuid));
	TestEqual(TEXT("Persisted uuid matches hook view"), PersistedUuid, SeenEventUuid);

	FString PersistedTimestamp;
	TestTrue(TEXT("Persisted event has timestamp"), EventObject->TryGetStringField(TEXT("timestamp"), PersistedTimestamp));
	TestEqual(TEXT("Persisted timestamp matches hook view"), PersistedTimestamp, SeenTimestamp);

	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	if (!TestTrue(TEXT("Persisted event has properties object"), EventObject->TryGetObjectField(TEXT("properties"), PropertiesObject)))
	{
		return false;
	}

	bool bBeforeSendValue = false;
	TestTrue(TEXT("Persisted properties include before_send"), (*PropertiesObject)->TryGetBoolField(TEXT("before_send"), bBeforeSendValue));
	TestTrue(TEXT("Persisted before_send is true"), bBeforeSendValue);
	TestFalse(TEXT("Persisted properties omit removed secret"), (*PropertiesObject)->HasField(TEXT("secret")));

	FString SourceValue;
	TestTrue(TEXT("Persisted properties keep caller source"), (*PropertiesObject)->TryGetStringField(TEXT("source"), SourceValue));
	TestEqual(TEXT("Persisted caller source is unchanged"), SourceValue, TEXT("caller"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerBeforeSendDropTest, "UnrealHog.Consent.ConsentController.BeforeSendDropPreventsQueueAndSend", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerBeforeSendDropTest::RunTest(const FString& Parameters)
{
	FScopedConsentTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeStorageFactory(Fixture.GetRootPath()), MakeTransportFactory(LastTransport), MakeUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeTransientSettings(true, true, false);

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);

	bool bHookCalled = false;
	FPostHogBeforeSendDelegate BeforeSend;
	BeforeSend.BindLambda([&](FPostHogBeforeSendEvent&)
	{
		bHookCalled = true;
		return EPostHogBeforeSendResult::Drop;
	});
	Controller.SetBeforeSend(MoveTemp(BeforeSend));

	const EPostHogCaptureResult Result = Controller.CaptureEvent(TEXT("drop-me"), nullptr, false);

	TestEqual(TEXT("Capture reports before-send drop"), Result, EPostHogCaptureResult::DroppedByBeforeSend);
	TestTrue(TEXT("Before-send hook was invoked"), bHookCalled);
	TestEqual(TEXT("No events queued"), Controller.GetQueuedEventCount(), 0);
	TestFalse(TEXT("No queue directory created"), IFileManager::Get().DirectoryExists(*Fixture.GetQueueDirectory()));
	if (!TestNotNull(TEXT("Transport exists from opt-in"), LastTransport))
	{
		return false;
	}
	TestEqual(TEXT("No batch sent"), LastTransport->GetSentCount(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerBeforeSendFailureTest, "UnrealHog.Consent.ConsentController.BeforeSendFailurePreventsQueueAndSend", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerBeforeSendFailureTest::RunTest(const FString& Parameters)
{
	FScopedConsentTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeStorageFactory(Fixture.GetRootPath()), MakeTransportFactory(LastTransport), MakeUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeTransientSettings(true, true, false);

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);

	bool bHookCalled = false;
	FPostHogBeforeSendDelegate BeforeSend;
	BeforeSend.BindLambda([&](FPostHogBeforeSendEvent&)
	{
		bHookCalled = true;
		return EPostHogBeforeSendResult::Failure;
	});
	Controller.SetBeforeSend(MoveTemp(BeforeSend));

	AddExpectedError(TEXT("PostHog before-send callback reported failure for event fail-me; dropping event before persistence."), EAutomationExpectedErrorFlags::Contains, 1);

	const EPostHogCaptureResult Result = Controller.CaptureEvent(TEXT("fail-me"), nullptr, false);

	TestEqual(TEXT("Capture reports before-send failure"), Result, EPostHogCaptureResult::BeforeSendFailed);
	TestTrue(TEXT("Before-send hook was invoked"), bHookCalled);
	TestEqual(TEXT("No events queued"), Controller.GetQueuedEventCount(), 0);
	TestFalse(TEXT("No queue directory created"), IFileManager::Get().DirectoryExists(*Fixture.GetQueueDirectory()));
	if (!TestNotNull(TEXT("Transport exists from opt-in"), LastTransport))
	{
		return false;
	}
	TestEqual(TEXT("No batch sent"), LastTransport->GetSentCount(), 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
