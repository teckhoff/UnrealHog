#include "Subsystems/PostHogRuntimeSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Consent/PostHogConsentController.h"
#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "PostHogDeveloperSettings.h"
#include "Reachability/PostHogReachabilityProvider.h"
#include "Subsystems/PostHogRuntimeSubsystemFlushOutcome.h"
#include "Tests/PostHogFakeBatchTransport.h"
#include "Tests/PostHogFakeReachabilityProvider.h"
#include "Tests/PostHogInMemoryStorageProvider.h"
#include "Tests/PostHogTestPropertyHelpers.h"
#include "UObject/Package.h"

namespace
{
	UPostHogDeveloperSettings* MakeRuntimeFlushSettings()
	{
		UPostHogDeveloperSettings* Settings = NewObject<UPostHogDeveloperSettings>(GetTransientPackage());
		UnrealHogTests::SetPropertyValue<FString>(Settings, TEXT("ApiKey"), TEXT("phc_valid_key"));
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bAnalyticsEnabled"), true);
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bDefaultUserOptIn"), false);
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bCaptureApplicationLifecycleEvents"), false);
		UnrealHogTests::SetPropertyValue<int32>(Settings, TEXT("MaxQueueSize"), 50);
		UnrealHogTests::SetPropertyValue<int32>(Settings, TEXT("MaxBatchSize"), 10);
		UnrealHogTests::SetPropertyValue<int32>(Settings, TEXT("FlushEventCount"), 100);
		return Settings;
	}

	FPostHogConsentController::FStorageProviderFactory MakeRuntimeFlushStorageFactory()
	{
		return []() -> TUniquePtr<IPostHogStorageProvider>
		{
			return MakeUnique<FPostHogInMemoryStorageProvider>();
		};
	}

	FPostHogConsentController::FTransportFactory MakeRuntimeFlushTransportFactory(FPostHogFakeBatchTransport*& OutLastTransport)
	{
		return [&OutLastTransport](const FString&) -> TUniquePtr<IPostHogBatchTransport>
		{
			TUniquePtr<FPostHogFakeBatchTransport> Transport = MakeUnique<FPostHogFakeBatchTransport>();
			OutLastTransport = Transport.Get();
			return Transport;
		};
	}

	FPostHogConsentController::FUuidGenerator MakeRuntimeFlushUuidGenerator()
	{
		const TSharedRef<int32> Counter = MakeShared<int32>(0);
		return [Counter]()
		{
			return FString::Printf(TEXT("runtime-flush-uuid-%d"), ++(*Counter));
		};
	}

	FPostHogConsentController::FReachabilityProviderFactory MakeRuntimeFlushReachabilityFactory(EPostHogReachabilityState State)
	{
		return [State]() -> TUniquePtr<IPostHogReachabilityProvider>
		{
			return MakeUnique<FPostHogFakeReachabilityProvider>(State);
		};
	}

	TUniquePtr<FPostHogConsentController> MakeOptedInRuntimeFlushController(UPostHogDeveloperSettings& Settings,
		EPostHogReachabilityState State,
		FPostHogFakeBatchTransport*& OutLastTransport)
	{
		TUniquePtr<FPostHogConsentController> Controller = MakeUnique<FPostHogConsentController>(
			MakeRuntimeFlushStorageFactory(),
			MakeRuntimeFlushTransportFactory(OutLastTransport),
			MakeRuntimeFlushUuidGenerator(),
			nullptr,
			MakeRuntimeFlushReachabilityFactory(State));

		Controller->Initialize(Settings);
		Controller->SetOptIn(true, Settings);
		return Controller;
	}

	UPostHogRuntimeSubsystem* MakeRuntimeFlushSubsystem()
	{
		UGameInstance* GameInstance = NewObject<UGameInstance>(GetTransientPackage());
		return NewObject<UPostHogRuntimeSubsystem>(GameInstance);
	}

	struct FRecordedSubsystemFlushCompletion
	{
		int32 CallCount = 0;
		EPostHogFlushOutcome LastOutcome = EPostHogFlushOutcome::Empty;

		FPostHogFlushCompletedDelegate MakeDelegate()
		{
			FPostHogFlushCompletedDelegate Delegate;
			Delegate.BindLambda([this](EPostHogFlushOutcome Outcome)
			{
				++CallCount;
				LastOutcome = Outcome;
			});
			return Delegate;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogRuntimeSubsystemManualFlushKnownOfflineReportsSkippedOfflineTest, "UnrealHog.Subsystems.RuntimeSubsystem.ManualFlushKnownOfflineReportsSkippedOffline", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogRuntimeSubsystemManualFlushKnownOfflineReportsSkippedOfflineTest::RunTest(const FString& Parameters)
{
	UPostHogRuntimeSubsystem* Subsystem = MakeRuntimeFlushSubsystem();
	ON_SCOPE_EXIT
	{
		if (Subsystem)
		{
			Subsystem->ResetConsentControllerForTests();
		}
	};

	UPostHogDeveloperSettings* Settings = MakeRuntimeFlushSettings();
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	TUniquePtr<FPostHogConsentController> Controller = MakeOptedInRuntimeFlushController(*Settings, EPostHogReachabilityState::NotReachable, LastTransport);
	TestTrue(TEXT("Controller opted in"), Controller->IsOptedIn());
	Subsystem->SetConsentControllerForTests(MoveTemp(Controller));

	Subsystem->CaptureEvent(TEXT("runtime-offline-flush-test"));
	TestEqual(TEXT("One event queued before flush"), Subsystem->GetQueuedEventCountForTests(), 1);
	if (!TestNotNull(TEXT("Transport created by opt-in"), LastTransport))
	{
		return false;
	}
	TestEqual(TEXT("No request sent before manual flush"), LastTransport->GetTotalSendCount(), 0);

	FRecordedSubsystemFlushCompletion Completion;
	const EPostHogFlushRequestResult RequestResult = Subsystem->Flush(Completion.MakeDelegate());

	TestEqual(TEXT("Flush request is accepted as Started"), RequestResult, EPostHogFlushRequestResult::Started);
	TestEqual(TEXT("Completion fired exactly once"), Completion.CallCount, 1);
	TestEqual(TEXT("Completion reports SkippedOffline"), Completion.LastOutcome, EPostHogFlushOutcome::SkippedOffline);
	TestEqual(TEXT("Offline flush preserves queued record"), Subsystem->GetQueuedEventCountForTests(), 1);
	TestEqual(TEXT("Offline flush sends no HTTP request"), LastTransport->GetTotalSendCount(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogRuntimeSubsystemManualFlushEmptyQueueReportsEmptyTest, "UnrealHog.Subsystems.RuntimeSubsystem.ManualFlushEmptyQueueReportsEmpty", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogRuntimeSubsystemManualFlushEmptyQueueReportsEmptyTest::RunTest(const FString& Parameters)
{
	UPostHogRuntimeSubsystem* Subsystem = MakeRuntimeFlushSubsystem();
	ON_SCOPE_EXIT
	{
		if (Subsystem)
		{
			Subsystem->ResetConsentControllerForTests();
		}
	};

	UPostHogDeveloperSettings* Settings = MakeRuntimeFlushSettings();
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	TUniquePtr<FPostHogConsentController> Controller = MakeOptedInRuntimeFlushController(*Settings, EPostHogReachabilityState::NotReachable, LastTransport);
	TestTrue(TEXT("Controller opted in"), Controller->IsOptedIn());
	Subsystem->SetConsentControllerForTests(MoveTemp(Controller));

	TestEqual(TEXT("No events queued before flush"), Subsystem->GetQueuedEventCountForTests(), 0);
	if (!TestNotNull(TEXT("Transport created by opt-in"), LastTransport))
	{
		return false;
	}

	FRecordedSubsystemFlushCompletion Completion;
	const EPostHogFlushRequestResult RequestResult = Subsystem->Flush(Completion.MakeDelegate());

	TestEqual(TEXT("Empty flush request is Skipped"), RequestResult, EPostHogFlushRequestResult::Skipped);
	TestEqual(TEXT("Completion fired exactly once"), Completion.CallCount, 1);
	TestEqual(TEXT("Completion reports Empty"), Completion.LastOutcome, EPostHogFlushOutcome::Empty);
	TestEqual(TEXT("Empty flush sends no HTTP request"), LastTransport->GetTotalSendCount(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogRuntimeSubsystemFlushOutcomeMappingRetainsExistingValuesTest, "UnrealHog.Subsystems.RuntimeSubsystem.FlushOutcomeMappingRetainsExistingValues", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogRuntimeSubsystemFlushOutcomeMappingRetainsExistingValuesTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Drained ordinal remains stable"), static_cast<int32>(EPostHogFlushOutcome::Drained), 0);
	TestEqual(TEXT("Empty ordinal remains stable"), static_cast<int32>(EPostHogFlushOutcome::Empty), 1);
	TestEqual(TEXT("Failed ordinal remains stable"), static_cast<int32>(EPostHogFlushOutcome::Failed), 2);
	TestEqual(TEXT("Cancelled ordinal remains stable"), static_cast<int32>(EPostHogFlushOutcome::Cancelled), 3);
	TestEqual(TEXT("ProgressBlocked ordinal remains stable"), static_cast<int32>(EPostHogFlushOutcome::ProgressBlocked), 4);
	TestEqual(TEXT("Paused ordinal remains stable"), static_cast<int32>(EPostHogFlushOutcome::Paused), 5);
	TestEqual(TEXT("SkippedOffline is appended"), static_cast<int32>(EPostHogFlushOutcome::SkippedOffline), 6);

	TestEqual(TEXT("Drained maps one-to-one"), TranslateFlushOutcome(EPostHogEventQueueFlushResult::Drained), EPostHogFlushOutcome::Drained);
	TestEqual(TEXT("Empty maps one-to-one"), TranslateFlushOutcome(EPostHogEventQueueFlushResult::Empty), EPostHogFlushOutcome::Empty);
	TestEqual(TEXT("Failed maps one-to-one"), TranslateFlushOutcome(EPostHogEventQueueFlushResult::Failed), EPostHogFlushOutcome::Failed);
	TestEqual(TEXT("Cancelled maps one-to-one"), TranslateFlushOutcome(EPostHogEventQueueFlushResult::Cancelled), EPostHogFlushOutcome::Cancelled);
	TestEqual(TEXT("ProgressBlocked maps one-to-one"), TranslateFlushOutcome(EPostHogEventQueueFlushResult::ProgressBlocked), EPostHogFlushOutcome::ProgressBlocked);
	TestEqual(TEXT("Paused maps one-to-one"), TranslateFlushOutcome(EPostHogEventQueueFlushResult::Paused), EPostHogFlushOutcome::Paused);
	TestEqual(TEXT("SkippedOffline maps one-to-one"), TranslateFlushOutcome(EPostHogEventQueueFlushResult::SkippedOffline), EPostHogFlushOutcome::SkippedOffline);
	TestEqual(TEXT("Unknown queue result maps to Empty"), TranslateFlushOutcome(static_cast<EPostHogEventQueueFlushResult>(255)), EPostHogFlushOutcome::Empty);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
