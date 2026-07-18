#include "Consent/PostHogConsentController.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Events/PostHogEvent.h"
#include "Events/PostHogEventQueue.h"
#include "PostHogDeveloperSettings.h"
#include "Storage/PostHogFileStorageProvider.h"
#include "Tests/PostHogFakeBatchTransport.h"
#include "Tests/PostHogTestPropertyHelpers.h"
#include "UObject/Package.h"

// EP-026: the public manual-flush API (UPostHogRuntimeSubsystem::Flush and the shared timer
// callback) is a thin wrapper around FPostHogConsentController::RequestFlush, which is where
// the acceptance/coalescing/skip state machine actually lives. UPostHogRuntimeSubsystem cannot
// be instantiated directly in Automation tests: it declares UCLASS(Within=GameInstance), so
// NewObject requires a live UGameInstance, and a bare NewObject<UGameInstance>() never has
// Init() called (see PostHogRuntimeSubsystemGatingTests.cpp), leaving GetSubsystem<>() null.
// These tests therefore exercise the shared state machine directly on the controller, matching
// this file's sibling PostHogConsentController*Tests.cpp files, which test every other
// controller-owned behavior the same way.

namespace
{
	// RAII fixture that owns a unique temporary directory for the file storage provider backing
	// these flush tests; removed on scope exit even if an assertion fails mid-test.
	class FScopedFlushTestStorageDirectory
	{
	public:
		FScopedFlushTestStorageDirectory()
			: RootPath(FPaths::Combine(FPlatformProcess::UserTempDir(), TEXT("UnrealHogFlushTests"), FGuid::NewGuid().ToString(EGuidFormats::Digits)))
		{
		}

		~FScopedFlushTestStorageDirectory()
		{
			IFileManager::Get().DeleteDirectory(*RootPath, false, true);
		}

		const FString& GetRootPath() const { return RootPath; }

	private:
		FString RootPath;
	};

	UPostHogDeveloperSettings* MakeFlushTestSettings(int32 MaxBatchSize, int32 FlushEventCount)
	{
		UPostHogDeveloperSettings* Settings = NewObject<UPostHogDeveloperSettings>(GetTransientPackage());
		UnrealHogTests::SetPropertyValue<FString>(Settings, TEXT("ApiKey"), TEXT("phc_valid_key"));
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bAnalyticsEnabled"), true);
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bDefaultUserOptIn"), false);
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bCaptureApplicationLifecycleEvents"), false);
		UnrealHogTests::SetPropertyValue<int32>(Settings, TEXT("MaxBatchSize"), MaxBatchSize);
		UnrealHogTests::SetPropertyValue<int32>(Settings, TEXT("FlushEventCount"), FlushEventCount);
		return Settings;
	}

	FPostHogConsentController::FStorageProviderFactory MakeFlushStorageFactory(const FString& RootPath)
	{
		return [RootPath]() -> TUniquePtr<IPostHogStorageProvider>
		{
			return MakeUnique<FPostHogFileStorageProvider>(RootPath);
		};
	}

	// Captures the most recently created fake transport so tests can drive its completion callbacks.
	FPostHogConsentController::FTransportFactory MakeFlushTransportFactory(FPostHogFakeBatchTransport*& OutLastTransport)
	{
		return [&OutLastTransport](const FString&) -> TUniquePtr<IPostHogBatchTransport>
		{
			TUniquePtr<FPostHogFakeBatchTransport> Transport = MakeUnique<FPostHogFakeBatchTransport>();
			OutLastTransport = Transport.Get();
			return Transport;
		};
	}

	// Deterministic, countable stand-in for PostHogUuidV7::New().
	FPostHogConsentController::FUuidGenerator MakeFlushUuidGenerator(int32& Counter)
	{
		return [&Counter]() { return FString::Printf(TEXT("flush-uuid-%d"), ++Counter); };
	}

	FPostHogEvent MakeFlushTestEvent(const FString& Suffix)
	{
		return FPostHogEvent(FString::Printf(TEXT("flush-test-event-%s"), *Suffix), TEXT("distinct-id"));
	}

	// Records how many times, and with what final result, a flush completion callback fired.
	struct FRecordedFlushCompletion
	{
		int32 CallCount = 0;
		EPostHogEventQueueFlushResult LastResult = EPostHogEventQueueFlushResult::Empty;

		FPostHogEventQueueFlushComplete MakeCallback()
		{
			return [this](EPostHogEventQueueFlushResult Result)
			{
				++CallCount;
				LastResult = Result;
			};
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerRequestFlushMultiBatchDrainsOnSuccessTest, "UnrealHog.Consent.ConsentController.RequestFlushMultiBatchDrainsOnSuccess", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerRequestFlushMultiBatchDrainsOnSuccessTest::RunTest(const FString& Parameters)
{
	FScopedFlushTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeFlushStorageFactory(Fixture.GetRootPath()), MakeFlushTransportFactory(LastTransport), MakeFlushUuidGenerator(UuidCounter));
	// MaxBatchSize=1 forces two events into two separate batches; FlushEventCount=100 keeps
	// Capture() from auto-flushing before the test issues its own manual RequestFlush call.
	UPostHogDeveloperSettings* Settings = MakeFlushTestSettings(1, 100);

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);

	Controller.Capture(MakeFlushTestEvent(TEXT("1")));
	Controller.Capture(MakeFlushTestEvent(TEXT("2")));
	TestEqual(TEXT("Two events queued before flush"), Controller.GetQueuedEventCount(), 2);

	FRecordedFlushCompletion Completion;
	const EPostHogConsentFlushRequestResult RequestResult = Controller.RequestFlush(Completion.MakeCallback());

	TestEqual(TEXT("Flush request is accepted as Started"), RequestResult, EPostHogConsentFlushRequestResult::Started);
	if (!TestNotNull(TEXT("Transport created"), LastTransport))
	{
		return false;
	}
	TestEqual(TEXT("First batch sent"), LastTransport->GetTotalSendCount(), 1);

	LastTransport->CompleteLast(true, 200, TEXT(""));
	TestEqual(TEXT("Second batch sent after first success"), LastTransport->GetTotalSendCount(), 2);

	LastTransport->CompleteLast(true, 200, TEXT(""));

	TestEqual(TEXT("Completion callback fired exactly once"), Completion.CallCount, 1);
	TestEqual(TEXT("Completion reports Drained"), Completion.LastResult, EPostHogEventQueueFlushResult::Drained);
	TestEqual(TEXT("Queue is empty after full drain"), Controller.GetQueuedEventCount(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerRequestFlushConcurrentCallsCoalesceTest, "UnrealHog.Consent.ConsentController.RequestFlushConcurrentCallsCoalesceAndBothCompleteOnce", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerRequestFlushConcurrentCallsCoalesceTest::RunTest(const FString& Parameters)
{
	FScopedFlushTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeFlushStorageFactory(Fixture.GetRootPath()), MakeFlushTransportFactory(LastTransport), MakeFlushUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeFlushTestSettings(50, 100);

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);

	Controller.Capture(MakeFlushTestEvent(TEXT("1")));

	FRecordedFlushCompletion FirstCompletion;
	const EPostHogConsentFlushRequestResult FirstResult = Controller.RequestFlush(FirstCompletion.MakeCallback());
	TestEqual(TEXT("First call is accepted as Started"), FirstResult, EPostHogConsentFlushRequestResult::Started);
	if (!TestNotNull(TEXT("Transport created"), LastTransport))
	{
		return false;
	}
	TestEqual(TEXT("First call sends one batch"), LastTransport->GetTotalSendCount(), 1);

	FRecordedFlushCompletion SecondCompletion;
	const EPostHogConsentFlushRequestResult SecondResult = Controller.RequestFlush(SecondCompletion.MakeCallback());
	TestEqual(TEXT("Second concurrent call reports AlreadyInProgress"), SecondResult, EPostHogConsentFlushRequestResult::AlreadyInProgress);
	TestEqual(TEXT("Second concurrent call creates no additional send"), LastTransport->GetTotalSendCount(), 1);
	TestEqual(TEXT("Second call has not completed yet"), SecondCompletion.CallCount, 0);

	LastTransport->CompleteLast(true, 200, TEXT(""));

	TestEqual(TEXT("First completion fired exactly once"), FirstCompletion.CallCount, 1);
	TestEqual(TEXT("Second completion fired exactly once"), SecondCompletion.CallCount, 1);
	TestEqual(TEXT("First completion reports Drained"), FirstCompletion.LastResult, EPostHogEventQueueFlushResult::Drained);
	TestEqual(TEXT("Second completion reports Drained"), SecondCompletion.LastResult, EPostHogEventQueueFlushResult::Drained);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerRequestFlushSkippedBeforeInitializeTest, "UnrealHog.Consent.ConsentController.RequestFlushSkippedBeforeInitialize", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerRequestFlushSkippedBeforeInitializeTest::RunTest(const FString& Parameters)
{
	FScopedFlushTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeFlushStorageFactory(Fixture.GetRootPath()), MakeFlushTransportFactory(LastTransport), MakeFlushUuidGenerator(UuidCounter));

	FRecordedFlushCompletion Completion;
	const EPostHogConsentFlushRequestResult Result = Controller.RequestFlush(Completion.MakeCallback());

	TestEqual(TEXT("Flush before Initialize is Skipped"), Result, EPostHogConsentFlushRequestResult::Skipped);
	TestEqual(TEXT("Completion fired synchronously exactly once"), Completion.CallCount, 1);
	TestEqual(TEXT("Completion reports Empty"), Completion.LastResult, EPostHogEventQueueFlushResult::Empty);
	TestNull(TEXT("No transport created"), LastTransport);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerRequestFlushSkippedWithoutConsentTest, "UnrealHog.Consent.ConsentController.RequestFlushSkippedWithoutConsent", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerRequestFlushSkippedWithoutConsentTest::RunTest(const FString& Parameters)
{
	FScopedFlushTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeFlushStorageFactory(Fixture.GetRootPath()), MakeFlushTransportFactory(LastTransport), MakeFlushUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeFlushTestSettings(50, 100);

	// Default opt-out: Initialize() is side-effect free, leaving the controller opted out.
	Controller.Initialize(*Settings);
	TestFalse(TEXT("Not opted in by default"), Controller.IsOptedIn());

	FRecordedFlushCompletion Completion;
	const EPostHogConsentFlushRequestResult Result = Controller.RequestFlush(Completion.MakeCallback());

	TestEqual(TEXT("Flush without consent is Skipped"), Result, EPostHogConsentFlushRequestResult::Skipped);
	TestEqual(TEXT("Completion fired synchronously exactly once"), Completion.CallCount, 1);
	TestEqual(TEXT("Completion reports Empty"), Completion.LastResult, EPostHogEventQueueFlushResult::Empty);
	TestNull(TEXT("No transport created"), LastTransport);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerRequestFlushSkippedWithEmptyQueueTest, "UnrealHog.Consent.ConsentController.RequestFlushSkippedWithEmptyQueue", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerRequestFlushSkippedWithEmptyQueueTest::RunTest(const FString& Parameters)
{
	FScopedFlushTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeFlushStorageFactory(Fixture.GetRootPath()), MakeFlushTransportFactory(LastTransport), MakeFlushUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeFlushTestSettings(50, 100);

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);
	TestEqual(TEXT("No events queued"), Controller.GetQueuedEventCount(), 0);
	if (!TestNotNull(TEXT("Transport created by opt-in"), LastTransport))
	{
		return false;
	}

	FRecordedFlushCompletion Completion;
	const EPostHogConsentFlushRequestResult Result = Controller.RequestFlush(Completion.MakeCallback());

	TestEqual(TEXT("Flush with an empty queue is Skipped"), Result, EPostHogConsentFlushRequestResult::Skipped);
	TestEqual(TEXT("Completion fired synchronously exactly once"), Completion.CallCount, 1);
	TestEqual(TEXT("Completion reports Empty"), Completion.LastResult, EPostHogEventQueueFlushResult::Empty);
	TestEqual(TEXT("No request created for an empty queue"), LastTransport->GetTotalSendCount(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerRequestFlushSkippedAfterShutdownTest, "UnrealHog.Consent.ConsentController.RequestFlushSkippedAfterShutdown", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerRequestFlushSkippedAfterShutdownTest::RunTest(const FString& Parameters)
{
	FScopedFlushTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeFlushStorageFactory(Fixture.GetRootPath()), MakeFlushTransportFactory(LastTransport), MakeFlushUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeFlushTestSettings(50, 100);

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);
	Controller.Capture(MakeFlushTestEvent(TEXT("1")));
	TestEqual(TEXT("One event queued before shutdown"), Controller.GetQueuedEventCount(), 1);
	if (!TestNotNull(TEXT("Transport created by opt-in"), LastTransport))
	{
		return false;
	}

	// Mirrors UPostHogRuntimeSubsystem::Deinitialize(), which calls Shutdown() but leaves
	// opt-in state, the queue, and pending records untouched.
	Controller.Shutdown();
	TestTrue(TEXT("Controller reports shutting down"), Controller.IsShuttingDown());

	FRecordedFlushCompletion Completion;
	const EPostHogConsentFlushRequestResult Result = Controller.RequestFlush(Completion.MakeCallback());

	TestEqual(TEXT("Flush after Shutdown is Skipped"), Result, EPostHogConsentFlushRequestResult::Skipped);
	TestEqual(TEXT("Completion fired synchronously exactly once"), Completion.CallCount, 1);
	TestEqual(TEXT("Completion reports Empty"), Completion.LastResult, EPostHogEventQueueFlushResult::Empty);
	TestEqual(TEXT("No request created after shutdown"), LastTransport->GetTotalSendCount(), 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
