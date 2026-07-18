#include "Consent/PostHogConsentController.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/CoreDelegates.h"
#include "Events/PostHogEvent.h"
#include "PostHogDeveloperSettings.h"
#include "Tests/PostHogFakeBatchTransport.h"
#include "Tests/PostHogInMemoryStorageProvider.h"
#include "Tests/PostHogTestPropertyHelpers.h"
#include "UObject/Package.h"

// EP-027: background must attempt a network flush and synchronously drain storage; terminate
// (and Shutdown() directly, which OnEnginePreExit/Deinitialize/the terminate delegate all funnel
// through) must never issue a new network request, only drain storage. These tests exercise
// FPostHogConsentController directly against fake collaborators, broadcasting the real
// FCoreDelegates so the delegate wiring itself is covered, matching this file's sibling
// PostHogConsentController*Tests.cpp files.

namespace
{
	UPostHogDeveloperSettings* MakeLifecycleFlushTestSettings()
	{
		UPostHogDeveloperSettings* Settings = NewObject<UPostHogDeveloperSettings>(GetTransientPackage());
		UnrealHogTests::SetPropertyValue<FString>(Settings, TEXT("ApiKey"), TEXT("phc_valid_key"));
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bAnalyticsEnabled"), true);
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bDefaultUserOptIn"), false);
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bCaptureApplicationLifecycleEvents"), false);
		// Large enough that the single test event never auto-flushes on Capture().
		UnrealHogTests::SetPropertyValue<int32>(Settings, TEXT("MaxBatchSize"), 50);
		UnrealHogTests::SetPropertyValue<int32>(Settings, TEXT("FlushEventCount"), 100);
		return Settings;
	}

	FPostHogConsentController::FStorageProviderFactory MakeLifecycleStorageFactory(FPostHogInMemoryStorageProvider*& OutLastStorage)
	{
		return [&OutLastStorage]() -> TUniquePtr<IPostHogStorageProvider>
		{
			TUniquePtr<FPostHogInMemoryStorageProvider> Storage = MakeUnique<FPostHogInMemoryStorageProvider>();
			OutLastStorage = Storage.Get();
			return Storage;
		};
	}

	FPostHogConsentController::FTransportFactory MakeLifecycleTransportFactory(FPostHogFakeBatchTransport*& OutLastTransport)
	{
		return [&OutLastTransport](const FString&) -> TUniquePtr<IPostHogBatchTransport>
		{
			TUniquePtr<FPostHogFakeBatchTransport> Transport = MakeUnique<FPostHogFakeBatchTransport>();
			OutLastTransport = Transport.Get();
			return Transport;
		};
	}

	FPostHogConsentController::FUuidGenerator MakeLifecycleUuidGenerator(int32& Counter)
	{
		return [&Counter]() { return FString::Printf(TEXT("lifecycle-flush-uuid-%d"), ++Counter); };
	}

	FPostHogEvent MakeLifecycleFlushTestEvent()
	{
		return FPostHogEvent(TEXT("lifecycle-flush-test-event"), TEXT("distinct-id"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerBackgroundFlushesAndDrainsStorageTest, "UnrealHog.Consent.ConsentController.ApplicationBackgroundRequestsFlushAndDrainsStorage", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerBackgroundFlushesAndDrainsStorageTest::RunTest(const FString& Parameters)
{
	FPostHogInMemoryStorageProvider* LastStorage = nullptr;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeLifecycleStorageFactory(LastStorage), MakeLifecycleTransportFactory(LastTransport), MakeLifecycleUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeLifecycleFlushTestSettings();

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);
	Controller.Capture(MakeLifecycleFlushTestEvent());

	if (!TestNotNull(TEXT("Storage provider created"), LastStorage) || !TestNotNull(TEXT("Transport created"), LastTransport))
	{
		return false;
	}

	TestEqual(TEXT("One event queued before background"), Controller.GetQueuedEventCount(), 1);
	const int32 SendCountBeforeBackground = LastTransport->GetTotalSendCount();
	const int32 DrainCountBeforeBackground = LastStorage->GetFlushPendingWritesCallCount();

	FCoreDelegates::ApplicationWillEnterBackgroundDelegate.Broadcast();

	TestEqual(TEXT("Background attempts a transport send"), LastTransport->GetTotalSendCount(), SendCountBeforeBackground + 1);
	TestEqual(TEXT("Background synchronously drains storage"), LastStorage->GetFlushPendingWritesCallCount(), DrainCountBeforeBackground + 1);

	// The in-flight best-effort request is still allowed to complete normally.
	LastTransport->CompleteLast(true, 200, TEXT(""));
	TestEqual(TEXT("Queue drained after background flush completes"), Controller.GetQueuedEventCount(), 0);

	Controller.Shutdown();

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerTerminateNeverIssuesNetworkRequestTest, "UnrealHog.Consent.ConsentController.ApplicationTerminateDrainsStorageWithoutNetworkRequest", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerTerminateNeverIssuesNetworkRequestTest::RunTest(const FString& Parameters)
{
	FPostHogInMemoryStorageProvider* LastStorage = nullptr;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeLifecycleStorageFactory(LastStorage), MakeLifecycleTransportFactory(LastTransport), MakeLifecycleUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeLifecycleFlushTestSettings();

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);
	Controller.Capture(MakeLifecycleFlushTestEvent());

	if (!TestNotNull(TEXT("Storage provider created"), LastStorage) || !TestNotNull(TEXT("Transport created"), LastTransport))
	{
		return false;
	}

	TestEqual(TEXT("One event queued before terminate"), Controller.GetQueuedEventCount(), 1);
	const int32 SendCountBeforeTerminate = LastTransport->GetTotalSendCount();

	FCoreDelegates::GetApplicationWillTerminateDelegate().Broadcast();

	TestEqual(TEXT("Terminate issues no new transport request"), LastTransport->GetTotalSendCount(), SendCountBeforeTerminate);
	TestTrue(TEXT("Terminate marks the controller as shutting down"), Controller.IsShuttingDown());
	TestTrue(TEXT("Terminate drains storage"), LastStorage->GetFlushPendingWritesCallCount() > 0);
	TestEqual(TEXT("Unsent event remains queued for next-launch rehydration"), Controller.GetQueuedEventCount(), 1);

	// Calling Shutdown() directly (as Deinitialize()/OnEnginePreExit do) must be idempotent: no
	// crash, no double-cancel, no additional network request, storage drained again.
	const int32 DrainCountAfterTerminate = LastStorage->GetFlushPendingWritesCallCount();
	Controller.Shutdown();

	TestEqual(TEXT("Second Shutdown() issues no new transport request"), LastTransport->GetTotalSendCount(), SendCountBeforeTerminate);
	TestTrue(TEXT("Second Shutdown() drains storage again"), LastStorage->GetFlushPendingWritesCallCount() > DrainCountAfterTerminate);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
