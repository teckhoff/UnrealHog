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
	// these super property tests; removed on scope exit even if an assertion fails mid-test.
	class FScopedSuperPropertiesTestStorageDirectory
	{
	public:
		FScopedSuperPropertiesTestStorageDirectory()
			: RootPath(FPaths::Combine(FPlatformProcess::UserTempDir(), TEXT("UnrealHogSuperPropertiesTests"), FGuid::NewGuid().ToString(EGuidFormats::Digits)))
		{
		}

		~FScopedSuperPropertiesTestStorageDirectory()
		{
			IFileManager::Get().DeleteDirectory(*RootPath, false, true);
		}

		const FString& GetRootPath() const { return RootPath; }

	private:
		FString RootPath;
	};

	UPostHogDeveloperSettings* MakeSuperPropertiesTestSettings()
	{
		UPostHogDeveloperSettings* Settings = NewObject<UPostHogDeveloperSettings>(GetTransientPackage());
		UnrealHogTests::SetPropertyValue<FString>(Settings, TEXT("ApiKey"), TEXT("phc_valid_key"));
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bAnalyticsEnabled"), true);
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bDefaultUserOptIn"), false);
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bCaptureApplicationLifecycleEvents"), false);
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bPreloadFeatureFlags"), false);
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bSessionReplay"), false);
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bReuseAnonymousId"), true);
		return Settings;
	}

	FPostHogConsentController::FStorageProviderFactory MakeSuperPropertiesStorageFactory(const FString& RootPath)
	{
		return [RootPath]() -> TUniquePtr<IPostHogStorageProvider>
		{
			return MakeUnique<FPostHogFileStorageProvider>(RootPath);
		};
	}

	// Captures the most recently created fake transport so tests can drive its completion callbacks.
	FPostHogConsentController::FTransportFactory MakeSuperPropertiesTransportFactory(FPostHogFakeBatchTransport*& OutLastTransport)
	{
		return [&OutLastTransport](const FString&) -> TUniquePtr<IPostHogBatchTransport>
		{
			TUniquePtr<FPostHogFakeBatchTransport> Transport = MakeUnique<FPostHogFakeBatchTransport>();
			OutLastTransport = Transport.Get();
			return Transport;
		};
	}

	// Deterministic, countable stand-in for PostHogUuidV7::New().
	FPostHogConsentController::FUuidGenerator MakeSuperPropertiesUuidGenerator(int32& Counter)
	{
		return [&Counter]() { return FString::Printf(TEXT("super-props-uuid-%d"), ++Counter); };
	}

	bool TryGetSuperPropertiesPayloadEvents(const FPostHogBatchPayload& Payload, TArray<TSharedPtr<FJsonObject>>& OutEvents)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerRegisterSuperPropertyRequiresConsentTest, "UnrealHog.Consent.ConsentController.RegisterSuperPropertyRequiresConsent", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerRegisterSuperPropertyRequiresConsentTest::RunTest(const FString& Parameters)
{
	FScopedSuperPropertiesTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeSuperPropertiesStorageFactory(Fixture.GetRootPath()), MakeSuperPropertiesTransportFactory(LastTransport), MakeSuperPropertiesUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeSuperPropertiesTestSettings();

	Controller.Initialize(*Settings);

	FPostHogEventProperty Value;
	Value.Type = EPostHogPropertyType::String;
	Value.StringValue = TEXT("bar");

	const bool bResult = Controller.RegisterSuperProperty(TEXT("foo"), Value);
	TestFalse(TEXT("Register before opt-in is rejected"), bResult);

	Controller.SetOptIn(true, *Settings);
	Controller.CaptureEvent(TEXT("post-optin-event"), nullptr);
	Controller.Flush();
	if (!TestNotNull(TEXT("Transport created"), LastTransport))
	{
		return false;
	}

	TArray<TSharedPtr<FJsonObject>> Events;
	TestTrue(TEXT("Payload parsed"), TryGetSuperPropertiesPayloadEvents(LastTransport->GetLastPayload(), Events));
	LastTransport->CompleteLast(true, 200, TEXT(""));

	TestEqual(TEXT("One event queued"), Events.Num(), 1);
	if (Events.Num() == 1)
	{
		const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
		TestTrue(TEXT("Event has properties"), Events[0]->TryGetObjectField(TEXT("properties"), PropertiesObject));
		TestFalse(TEXT("Rejected registration never appears on later events"), (*PropertiesObject)->HasField(TEXT("foo")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerCaptureEventIncludesSuperPropertyTest, "UnrealHog.Consent.ConsentController.CaptureEventIncludesRegisteredSuperProperty", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerCaptureEventIncludesSuperPropertyTest::RunTest(const FString& Parameters)
{
	FScopedSuperPropertiesTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeSuperPropertiesStorageFactory(Fixture.GetRootPath()), MakeSuperPropertiesTransportFactory(LastTransport), MakeSuperPropertiesUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeSuperPropertiesTestSettings();

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);

	FPostHogEventProperty Value;
	Value.Type = EPostHogPropertyType::String;
	Value.StringValue = TEXT("bar");
	TestTrue(TEXT("Register succeeds after opt-in"), Controller.RegisterSuperProperty(TEXT("foo"), Value));

	Controller.CaptureEvent(TEXT("my-event"), nullptr);
	Controller.Flush();
	if (!TestNotNull(TEXT("Transport created"), LastTransport))
	{
		return false;
	}

	TArray<TSharedPtr<FJsonObject>> Events;
	TestTrue(TEXT("Payload parsed"), TryGetSuperPropertiesPayloadEvents(LastTransport->GetLastPayload(), Events));
	LastTransport->CompleteLast(true, 200, TEXT(""));

	TestEqual(TEXT("One event queued"), Events.Num(), 1);
	if (Events.Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("Event has properties"), Events[0]->TryGetObjectField(TEXT("properties"), PropertiesObject));

	FString FooValue;
	TestTrue(TEXT("properties has foo"), (*PropertiesObject)->TryGetStringField(TEXT("foo"), FooValue));
	TestEqual(TEXT("foo value is the registered super property"), FooValue, FString(TEXT("bar")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerCallPropertyOverridesSuperPropertyTest, "UnrealHog.Consent.ConsentController.CallPropertyOverridesMatchingSuperProperty", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerCallPropertyOverridesSuperPropertyTest::RunTest(const FString& Parameters)
{
	FScopedSuperPropertiesTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeSuperPropertiesStorageFactory(Fixture.GetRootPath()), MakeSuperPropertiesTransportFactory(LastTransport), MakeSuperPropertiesUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeSuperPropertiesTestSettings();

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);

	FPostHogEventProperty Value;
	Value.Type = EPostHogPropertyType::String;
	Value.StringValue = TEXT("super");
	TestTrue(TEXT("Register succeeds"), Controller.RegisterSuperProperty(TEXT("k"), Value));

	UPostHogEventProperties* CallProperties = NewObject<UPostHogEventProperties>();
	CallProperties->AddString(TEXT("k"), TEXT("call"));

	const FString SessionIdBeforeCapture = Controller.GetSessionId();

	Controller.CaptureEvent(TEXT("my-event"), CallProperties);
	Controller.Flush();
	if (!TestNotNull(TEXT("Transport created"), LastTransport))
	{
		return false;
	}

	TArray<TSharedPtr<FJsonObject>> Events;
	TestTrue(TEXT("Payload parsed"), TryGetSuperPropertiesPayloadEvents(LastTransport->GetLastPayload(), Events));
	LastTransport->CompleteLast(true, 200, TEXT(""));

	TestEqual(TEXT("One event queued"), Events.Num(), 1);
	if (Events.Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("Event has properties"), Events[0]->TryGetObjectField(TEXT("properties"), PropertiesObject));

	FString KValue;
	TestTrue(TEXT("properties has k"), (*PropertiesObject)->TryGetStringField(TEXT("k"), KValue));
	TestEqual(TEXT("Call property overrides matching super property"), KValue, FString(TEXT("call")));

	FString SessionIdValue;
	TestTrue(TEXT("properties has $session_id"), (*PropertiesObject)->TryGetStringField(TEXT("$session_id"), SessionIdValue));
	TestEqual(TEXT("SDK-owned $session_id is unaffected by call/super properties"), SessionIdValue, SessionIdBeforeCapture);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerCallPropertyNeverOverridesSdkOwnedTest, "UnrealHog.Consent.ConsentController.CallPropertyNeverOverridesSdkOwnedSessionId", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerCallPropertyNeverOverridesSdkOwnedTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("protected PostHog property \"$session_id\""), EAutomationExpectedErrorFlags::Contains, 1, false);

	FScopedSuperPropertiesTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeSuperPropertiesStorageFactory(Fixture.GetRootPath()), MakeSuperPropertiesTransportFactory(LastTransport), MakeSuperPropertiesUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeSuperPropertiesTestSettings();

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);

	const FString RealSessionId = Controller.GetSessionId();

	UPostHogEventProperties* CallProperties = NewObject<UPostHogEventProperties>();
	CallProperties->AddString(TEXT("$session_id"), TEXT("attacker-supplied-session-id"));

	Controller.CaptureEvent(TEXT("my-event"), CallProperties);
	Controller.Flush();
	if (!TestNotNull(TEXT("Transport created"), LastTransport))
	{
		return false;
	}

	TArray<TSharedPtr<FJsonObject>> Events;
	TestTrue(TEXT("Payload parsed"), TryGetSuperPropertiesPayloadEvents(LastTransport->GetLastPayload(), Events));
	LastTransport->CompleteLast(true, 200, TEXT(""));

	TestEqual(TEXT("One event queued"), Events.Num(), 1);
	if (Events.Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("Event has properties"), Events[0]->TryGetObjectField(TEXT("properties"), PropertiesObject));

	FString SessionIdValue;
	TestTrue(TEXT("properties has $session_id"), (*PropertiesObject)->TryGetStringField(TEXT("$session_id"), SessionIdValue));
	TestEqual(TEXT("Call-supplied $session_id never overrides the SDK-owned session id"), SessionIdValue, RealSessionId);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerSuperPropertiesSurviveOptOutOptInCycleTest, "UnrealHog.Consent.ConsentController.SuperPropertiesSurviveOptOutOptInCycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerSuperPropertiesSurviveOptOutOptInCycleTest::RunTest(const FString& Parameters)
{
	FScopedSuperPropertiesTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeSuperPropertiesStorageFactory(Fixture.GetRootPath()), MakeSuperPropertiesTransportFactory(LastTransport), MakeSuperPropertiesUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeSuperPropertiesTestSettings();

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);

	FPostHogEventProperty Value;
	Value.Type = EPostHogPropertyType::String;
	Value.StringValue = TEXT("bar");
	TestTrue(TEXT("Register succeeds"), Controller.RegisterSuperProperty(TEXT("foo"), Value));

	Controller.SetOptIn(false, *Settings);
	Controller.SetOptIn(true, *Settings);

	Controller.CaptureEvent(TEXT("post-restart-event"), nullptr);
	Controller.Flush();
	if (!TestNotNull(TEXT("Transport created"), LastTransport))
	{
		return false;
	}

	TArray<TSharedPtr<FJsonObject>> Events;
	TestTrue(TEXT("Payload parsed"), TryGetSuperPropertiesPayloadEvents(LastTransport->GetLastPayload(), Events));
	LastTransport->CompleteLast(true, 200, TEXT(""));

	TestEqual(TEXT("One event queued"), Events.Num(), 1);
	if (Events.Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("Event has properties"), Events[0]->TryGetObjectField(TEXT("properties"), PropertiesObject));

	FString FooValue;
	TestTrue(TEXT("properties has foo after opt-out/opt-in cycle"), (*PropertiesObject)->TryGetStringField(TEXT("foo"), FooValue));
	TestEqual(TEXT("foo value survives the opt-out/opt-in cycle"), FooValue, FString(TEXT("bar")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerUnregisterSuperPropertyStopsFutureEventsTest, "UnrealHog.Consent.ConsentController.UnregisterSuperPropertyStopsAppearingOnFutureEvents", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerUnregisterSuperPropertyStopsFutureEventsTest::RunTest(const FString& Parameters)
{
	FScopedSuperPropertiesTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeSuperPropertiesStorageFactory(Fixture.GetRootPath()), MakeSuperPropertiesTransportFactory(LastTransport), MakeSuperPropertiesUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeSuperPropertiesTestSettings();

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);

	FPostHogEventProperty Value;
	Value.Type = EPostHogPropertyType::String;
	Value.StringValue = TEXT("bar");
	TestTrue(TEXT("Register succeeds"), Controller.RegisterSuperProperty(TEXT("foo"), Value));

	Controller.UnregisterSuperProperty(TEXT("foo"));

	Controller.CaptureEvent(TEXT("after-unregister-event"), nullptr);
	Controller.Flush();
	if (!TestNotNull(TEXT("Transport created"), LastTransport))
	{
		return false;
	}

	TArray<TSharedPtr<FJsonObject>> Events;
	TestTrue(TEXT("Payload parsed"), TryGetSuperPropertiesPayloadEvents(LastTransport->GetLastPayload(), Events));
	LastTransport->CompleteLast(true, 200, TEXT(""));

	TestEqual(TEXT("One event queued"), Events.Num(), 1);
	if (Events.Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("Event has properties"), Events[0]->TryGetObjectField(TEXT("properties"), PropertiesObject));
	TestFalse(TEXT("Unregistered property no longer appears on future events"), (*PropertiesObject)->HasField(TEXT("foo")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
