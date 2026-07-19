#include "Consent/PostHogConsentController.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Events/PostHogBatchPayload.h"
#include "Events/PostHogBeforeSend.h"
#include "Events/PostHogEventProperties.h"
#include "Events/PostHogExceptionInput.h"
#include "SDK/PostHogSdkInfo.h"
#include "Tests/PostHogAcceptanceFixture.h"

// EP-029: proves the completed event pipeline behaves as one observable system, exercised only
// through FPostHogConsentController's public producer APIs (the same "public subsystem" boundary
// every PostHogConsentController*Tests.cpp file already uses, since UPostHogRuntimeSubsystem
// cannot be constructed outside a live UGameInstance -- see PostHogConsentControllerFlushTests.cpp).
// This file covers consent gating and one continuous opt-in flow across every producer;
// PostHogCoreIngressRecoveryAcceptanceTests.cpp covers persistence, retry, and shutdown paths.

namespace
{
	bool CoreIngressConsent_TryGetPayloadEventNames(const FPostHogBatchPayload& Payload, TArray<FString>& OutEventNames)
	{
		const TSharedRef<FJsonObject> PayloadJson = Payload.ToJsonObject();

		const TArray<TSharedPtr<FJsonValue>>* BatchArray = nullptr;
		if (!PayloadJson->TryGetArrayField(TEXT("batch"), BatchArray))
		{
			return false;
		}

		OutEventNames.Reset(BatchArray->Num());
		for (const TSharedPtr<FJsonValue>& EventValue : *BatchArray)
		{
			const TSharedPtr<FJsonObject> EventObject = EventValue->AsObject();
			if (!EventObject.IsValid())
			{
				return false;
			}

			FString EventName;
			if (!EventObject->TryGetStringField(TEXT("event"), EventName))
			{
				return false;
			}

			OutEventNames.Add(EventName);
		}

		return true;
	}

	bool CoreIngressConsent_TryGetPayloadEvents(const FPostHogBatchPayload& Payload, TArray<TSharedPtr<FJsonObject>>& OutEvents)
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

	FPostHogExceptionInput MakeCoreIngressConsentTestException()
	{
		FPostHogExceptionInput Exception;
		Exception.Message = TEXT("acceptance-suite crash");
		Exception.Type = TEXT("AcceptanceError");
		Exception.bHandled = true;
		return Exception;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogCoreIngressDeniedConsentProducesNoIdentifiersRecordsOrRequestsTest, "UnrealHog.Acceptance.CoreIngress.DeniedConsentProducesNoIdentifiersRecordsOrRequests", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogCoreIngressDeniedConsentProducesNoIdentifiersRecordsOrRequestsTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("dropping event pre-consent-event"), EAutomationExpectedErrorFlags::Contains, 1, false);
	AddExpectedError(TEXT("dropping Identify for pre-consent-user"), EAutomationExpectedErrorFlags::Contains, 1, false);
	AddExpectedError(TEXT("dropping Group for company"), EAutomationExpectedErrorFlags::Contains, 1, false);
	AddExpectedError(TEXT("dropping event $screen"), EAutomationExpectedErrorFlags::Contains, 1, false);
	AddExpectedError(TEXT("dropping event $exception"), EAutomationExpectedErrorFlags::Contains, 1, false);

	FPostHogAcceptanceFixture Fixture;
	TUniquePtr<FPostHogConsentController> Controller = Fixture.MakeController();
	// bCaptureApplicationLifecycleEvents=true proves lifecycle producers stay silent too.
	UPostHogDeveloperSettings* Settings = FPostHogAcceptanceFixture::MakeSettings(true, true, false, true);

	Controller->Initialize(*Settings);

	TestFalse(TEXT("Not opted in by default"), Controller->IsOptedIn());

	const EPostHogCaptureResult CaptureResult = Controller->CaptureEvent(TEXT("pre-consent-event"), nullptr);
	const EPostHogCaptureResult IdentifyResult = Controller->Identify(TEXT("pre-consent-user"), nullptr, nullptr);
	const EPostHogCaptureResult GroupResult = Controller->Group(TEXT("company"), TEXT("acme"), nullptr);
	const EPostHogCaptureResult ScreenResult = Controller->CaptureScreen(TEXT("Main Menu"), nullptr);
	const EPostHogCaptureResult ExceptionResult = Controller->CaptureException(MakeCoreIngressConsentTestException(), nullptr);

	TestEqual(TEXT("CaptureEvent rejected before consent"), CaptureResult, EPostHogCaptureResult::NotOptedIn);
	TestEqual(TEXT("Identify rejected before consent"), IdentifyResult, EPostHogCaptureResult::NotOptedIn);
	TestEqual(TEXT("Group rejected before consent"), GroupResult, EPostHogCaptureResult::NotOptedIn);
	TestEqual(TEXT("CaptureScreen rejected before consent"), ScreenResult, EPostHogCaptureResult::NotOptedIn);
	TestEqual(TEXT("CaptureException rejected before consent"), ExceptionResult, EPostHogCaptureResult::NotOptedIn);

	TestEqual(TEXT("No transport created"), Controller->GetTransportCreationCount(), 0);
	TestEqual(TEXT("Identity manager never loaded"), Controller->GetIdentityManagerLoadCount(), 0);
	TestTrue(TEXT("No distinct id assigned"), Controller->GetDistinctId().IsEmpty());
	TestEqual(TEXT("No events queued"), Controller->GetQueuedEventCount(), 0);
	TestNull(TEXT("No transport was ever created by the factory"), Fixture.LastTransport);
	TestEqual(TEXT("No event records persisted"), Fixture.SharedStorage.GetEventCount(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogCoreIngressOptInEnablesFullPathAllProducersInOneFlowTest, "UnrealHog.Acceptance.CoreIngress.OptInEnablesFullPathAllProducersInOneFlow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogCoreIngressOptInEnablesFullPathAllProducersInOneFlowTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("protected PostHog property \"$lib\""), EAutomationExpectedErrorFlags::Contains, 1, false);

	FPostHogAcceptanceFixture Fixture;
	TUniquePtr<FPostHogConsentController> Controller = Fixture.MakeController();
	UPostHogDeveloperSettings* Settings = FPostHogAcceptanceFixture::MakeSettings(true, true, false, /*bCaptureApplicationLifecycleEvents*/ true);

	Controller->Initialize(*Settings);
	TestTrue(TEXT("Opt-in succeeds"), Controller->SetOptIn(true, *Settings));
	TestEqual(TEXT("Opt-in queues Application Installed and Opened"), Controller->GetQueuedEventCount(), 2);

	const FString ExpectedSessionId = Controller->GetSessionId();

	FPostHogEventProperty SuperPropertyValue;
	SuperPropertyValue.Type = EPostHogPropertyType::String;
	SuperPropertyValue.StringValue = TEXT("super-value");
	TestTrue(TEXT("Super property registers"), Controller->RegisterSuperProperty(TEXT("source"), SuperPropertyValue));

	TestEqual(TEXT("Identify succeeds"), Controller->Identify(TEXT("acceptance-user"), nullptr, nullptr), EPostHogCaptureResult::Success);
	TestEqual(TEXT("Group succeeds"), Controller->Group(TEXT("company"), TEXT("acme"), nullptr), EPostHogCaptureResult::Success);
	TestEqual(TEXT("CaptureScreen succeeds"), Controller->CaptureScreen(TEXT("Main Menu"), nullptr), EPostHogCaptureResult::Success);
	TestEqual(TEXT("CaptureException succeeds"), Controller->CaptureException(MakeCoreIngressConsentTestException(), nullptr), EPostHogCaptureResult::Success);

	UPostHogEventProperties* CallProperties = NewObject<UPostHogEventProperties>();
	// Call-level property collides with the registered super property: call must win.
	CallProperties->AddString(TEXT("source"), TEXT("call-value"));
	// Caller attempts to spoof an SDK-owned reserved property: SDK must win regardless of super/call.
	CallProperties->AddString(TEXT("$lib"), TEXT("attacker-lib"));

	bool bBeforeSendCalled = false;
	FString BeforeSendSawSource;
	FString BeforeSendSawLib;
	FPostHogBeforeSendDelegate BeforeSend;
	BeforeSend.BindLambda([&](FPostHogBeforeSendEvent& Event)
	{
		bBeforeSendCalled = true;
		Event.GetProperties().TryGetStringField(TEXT("source"), BeforeSendSawSource);
		Event.GetProperties().TryGetStringField(TEXT("$lib"), BeforeSendSawLib);
		Event.GetMutableProperties().SetBoolField(TEXT("before_send_seen"), true);
		return EPostHogBeforeSendResult::Continue;
	});
	Controller->SetBeforeSend(MoveTemp(BeforeSend));

	TestEqual(TEXT("Custom capture succeeds"), Controller->CaptureEvent(TEXT("custom-event"), CallProperties), EPostHogCaptureResult::Success);
	TestTrue(TEXT("Before-send hook observed the fully merged event"), bBeforeSendCalled);
	TestEqual(TEXT("Before-send sees call property beating super property"), BeforeSendSawSource, TEXT("call-value"));
	TestEqual(TEXT("Before-send sees SDK-owned $lib, not the caller's spoofed value"), BeforeSendSawLib, FPostHogSdkInfo::GetLibraryName());

	Controller->Flush();
	if (!TestNotNull(TEXT("Transport created"), Fixture.LastTransport))
	{
		return false;
	}

	TArray<FString> EventNames;
	TestTrue(TEXT("Payload event names parsed"), CoreIngressConsent_TryGetPayloadEventNames(Fixture.LastTransport->GetLastPayload(), EventNames));

	TArray<TSharedPtr<FJsonObject>> Events;
	TestTrue(TEXT("Payload events parsed"), CoreIngressConsent_TryGetPayloadEvents(Fixture.LastTransport->GetLastPayload(), Events));
	Fixture.LastTransport->CompleteLast(true, 200, TEXT(""));

	const TArray<FString> ExpectedOrder = {
		TEXT("Application Installed"),
		TEXT("Application Opened"),
		TEXT("$identify"),
		TEXT("$groupidentify"),
		TEXT("$screen"),
		TEXT("$exception"),
		TEXT("custom-event")
	};
	TestEqual(TEXT("Batch contains every producer's event, in call order"), EventNames.Num(), ExpectedOrder.Num());
	if (Events.Num() != ExpectedOrder.Num() || EventNames.Num() != ExpectedOrder.Num())
	{
		return false;
	}

	for (int32 Index = 0; Index < ExpectedOrder.Num(); ++Index)
	{
		TestEqual(*FString::Printf(TEXT("Event %d name matches call order"), Index), EventNames[Index], ExpectedOrder[Index]);
	}

	for (int32 Index = 0; Index < Events.Num(); ++Index)
	{
		const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
		if (!TestTrue(*FString::Printf(TEXT("Event %d has properties"), Index), Events[Index]->TryGetObjectField(TEXT("properties"), PropertiesObject)))
		{
			continue;
		}

		FString SessionId;
		TestTrue(*FString::Printf(TEXT("Event %d has $session_id"), Index), (*PropertiesObject)->TryGetStringField(TEXT("$session_id"), SessionId));
		TestEqual(*FString::Printf(TEXT("Event %d shares one session id"), Index), SessionId, ExpectedSessionId);
	}

	// Lifecycle events (0, 1) are captured before Identify, so they are still anonymous: under the
	// default IdentifiedOnly policy $process_person_profile must be explicitly false.
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
		Events[Index]->TryGetObjectField(TEXT("properties"), PropertiesObject);
		bool bProcessPersonProfile = true;
		TestTrue(*FString::Printf(TEXT("Lifecycle event %d has $process_person_profile"), Index), (*PropertiesObject)->TryGetBoolField(TEXT("$process_person_profile"), bProcessPersonProfile));
		TestFalse(*FString::Printf(TEXT("Lifecycle event %d is anonymous"), Index), bProcessPersonProfile);
	}

	// The custom event (last) is captured after Identify, so under IdentifiedOnly the field is
	// omitted entirely (implicit true), matching PostHogPersonProfilePolicyTests.cpp's convention.
	const TSharedPtr<FJsonObject>* CustomPropertiesObject = nullptr;
	Events.Last()->TryGetObjectField(TEXT("properties"), CustomPropertiesObject);
	TestFalse(TEXT("Identified custom event omits $process_person_profile"), (*CustomPropertiesObject)->HasField(TEXT("$process_person_profile")));

	FString CustomSource;
	TestTrue(TEXT("Custom event has source"), (*CustomPropertiesObject)->TryGetStringField(TEXT("source"), CustomSource));
	TestEqual(TEXT("Call property beats registered super property"), CustomSource, TEXT("call-value"));

	FString CustomLib;
	TestTrue(TEXT("Custom event has $lib"), (*CustomPropertiesObject)->TryGetStringField(TEXT("$lib"), CustomLib));
	TestEqual(TEXT("SDK-owned $lib wins over caller and before-send never sees the spoofed value"), CustomLib, FPostHogSdkInfo::GetLibraryName());

	bool bBeforeSendSeen = false;
	TestTrue(TEXT("Custom event carries the before-send mutation"), (*CustomPropertiesObject)->TryGetBoolField(TEXT("before_send_seen"), bBeforeSendSeen));
	TestTrue(TEXT("before_send_seen persisted as true"), bBeforeSendSeen);

	const TSharedPtr<FJsonObject>* IdentifyPropertiesObject = nullptr;
	Events[2]->TryGetObjectField(TEXT("properties"), IdentifyPropertiesObject);
	FString IdentifySource;
	TestTrue(TEXT("$identify carries the registered super property"), (*IdentifyPropertiesObject)->TryGetStringField(TEXT("source"), IdentifySource));
	TestEqual(TEXT("$identify sees the super property (registered before it was captured)"), IdentifySource, TEXT("super-value"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogCoreIngressOptOutClearsQueueAndBlocksFurtherCaptureAcrossProducersTest, "UnrealHog.Acceptance.CoreIngress.OptOutClearsQueueAndBlocksFurtherCaptureAcrossProducers", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogCoreIngressOptOutClearsQueueAndBlocksFurtherCaptureAcrossProducersTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("dropping event post-optout-event"), EAutomationExpectedErrorFlags::Contains, 1, false);
	AddExpectedError(TEXT("dropping Identify for post-optout-user"), EAutomationExpectedErrorFlags::Contains, 1, false);
	AddExpectedError(TEXT("dropping Group for company"), EAutomationExpectedErrorFlags::Contains, 1, false);
	AddExpectedError(TEXT("dropping event $screen"), EAutomationExpectedErrorFlags::Contains, 1, false);
	AddExpectedError(TEXT("dropping event $exception"), EAutomationExpectedErrorFlags::Contains, 1, false);

	FPostHogAcceptanceFixture Fixture;
	TUniquePtr<FPostHogConsentController> Controller = Fixture.MakeController();
	UPostHogDeveloperSettings* Settings = FPostHogAcceptanceFixture::MakeSettings(true, true, false, false);

	Controller->Initialize(*Settings);
	Controller->SetOptIn(true, *Settings);

	Controller->CaptureEvent(TEXT("pre-optout-event"), nullptr);
	Controller->Identify(TEXT("pre-optout-user"), nullptr, nullptr);
	Controller->Group(TEXT("company"), TEXT("acme"), nullptr);
	TestTrue(TEXT("Events queued before opt-out"), Controller->GetQueuedEventCount() > 0);

	TestTrue(TEXT("Opt-out succeeds"), Controller->SetOptIn(false, *Settings));
	TestFalse(TEXT("No longer opted in"), Controller->IsOptedIn());
	TestEqual(TEXT("Queue cleared on opt-out"), Controller->GetQueuedEventCount(), 0);
	TestNull(TEXT("Event queue released on opt-out"), Controller->GetEventQueue());

	TestEqual(TEXT("CaptureEvent blocked after opt-out"), Controller->CaptureEvent(TEXT("post-optout-event"), nullptr), EPostHogCaptureResult::NotOptedIn);
	TestEqual(TEXT("Identify blocked after opt-out"), Controller->Identify(TEXT("post-optout-user"), nullptr, nullptr), EPostHogCaptureResult::NotOptedIn);
	TestEqual(TEXT("Group blocked after opt-out"), Controller->Group(TEXT("company"), TEXT("acme"), nullptr), EPostHogCaptureResult::NotOptedIn);
	TestEqual(TEXT("CaptureScreen blocked after opt-out"), Controller->CaptureScreen(TEXT("Main Menu"), nullptr), EPostHogCaptureResult::NotOptedIn);
	TestEqual(TEXT("CaptureException blocked after opt-out"), Controller->CaptureException(MakeCoreIngressConsentTestException(), nullptr), EPostHogCaptureResult::NotOptedIn);
	TestEqual(TEXT("No further events queued after opt-out"), Controller->GetQueuedEventCount(), 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
