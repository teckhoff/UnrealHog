#include "Events/PostHogEventQueue.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Events/PostHogBatchPayload.h"
#include "Events/PostHogEvent.h"
#include "Storage/PostHogFileStorageProvider.h"
#include "Tests/PostHogFakeBatchTransport.h"

namespace
{
	// RAII fixture that owns a unique temporary directory for the file storage provider backing
	// these queue tests; removed on scope exit even if an assertion fails mid-test.
	class FScopedQueueTestStorageDirectory
	{
	public:
		FScopedQueueTestStorageDirectory()
			: RootPath(FPaths::Combine(FPlatformProcess::UserTempDir(), TEXT("UnrealHogTests"), FGuid::NewGuid().ToString(EGuidFormats::Digits)))
		{
		}

		~FScopedQueueTestStorageDirectory()
		{
			IFileManager::Get().DeleteDirectory(*RootPath, false, true);
		}

		const FString& GetRootPath() const { return RootPath; }

	private:
		FString RootPath;
	};

	FPostHogEvent MakeTestEvent(const FString& Suffix)
	{
		return FPostHogEvent(FString::Printf(TEXT("test-event-%s"), *Suffix), TEXT("distinct-id"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueueFlushesAtFlushEventCountTest, "UnrealHog.Events.EventQueue.EnqueueTriggersFlushAtFlushEventCount", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueueFlushesAtFlushEventCountTest::RunTest(const FString& Parameters)
{
	FScopedQueueTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Storage(Fixture.GetRootPath());
	FPostHogFakeBatchTransport Transport;

	FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 100, 100, 3);

	Queue.Enqueue(MakeTestEvent(TEXT("1")));
	Queue.Enqueue(MakeTestEvent(TEXT("2")));
	TestEqual(TEXT("No flush before reaching FlushEventCount"), Transport.GetSentCount(), 0);

	Queue.Enqueue(MakeTestEvent(TEXT("3")));
	TestEqual(TEXT("Exactly one flush at FlushEventCount"), Transport.GetSentCount(), 1);
	TestEqual(TEXT("Batch contains all queued events"), Transport.GetLastPayload().Num(), 3);

	FString ApiKey;
	Transport.GetLastPayload().ToJsonObject()->TryGetStringField(TEXT("api_key"), ApiKey);
	TestEqual(TEXT("Batch carries the queue's API key"), ApiKey, TEXT("test-api-key"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueueSuccessClearsQueueTest, "UnrealHog.Events.EventQueue.SuccessfulCompletionClearsQueueAndStorage", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueueSuccessClearsQueueTest::RunTest(const FString& Parameters)
{
	FScopedQueueTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Storage(Fixture.GetRootPath());
	FPostHogFakeBatchTransport Transport;

	FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 100, 100, 2);

	Queue.Enqueue(MakeTestEvent(TEXT("1")));
	Queue.Enqueue(MakeTestEvent(TEXT("2")));
	TestEqual(TEXT("Flush occurred"), Transport.GetSentCount(), 1);
	TestEqual(TEXT("Events still counted while in flight"), Queue.Num(), 2);

	Transport.CompleteLast(true, 200, TEXT(""));

	TestEqual(TEXT("Queue empties on success"), Queue.Num(), 0);
	TestEqual(TEXT("Storage no longer reports the flushed events"), Storage.GetEventCount(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueueFailureRetainsQueueTest, "UnrealHog.Events.EventQueue.FailedCompletionRetainsQueueAndAllowsRetry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueueFailureRetainsQueueTest::RunTest(const FString& Parameters)
{
	FScopedQueueTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Storage(Fixture.GetRootPath());
	FPostHogFakeBatchTransport Transport;

	FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 100, 100, 2);

	Queue.Enqueue(MakeTestEvent(TEXT("1")));
	Queue.Enqueue(MakeTestEvent(TEXT("2")));
	TestEqual(TEXT("Flush occurred"), Transport.GetSentCount(), 1);

	Transport.CompleteLast(false, 500, TEXT(""));

	TestEqual(TEXT("Failed flush leaves events queued"), Queue.Num(), 2);
	TestEqual(TEXT("Storage retains the un-acknowledged events"), Storage.GetEventCount(), 2);

	// bIsFlushing must have reset so a later Flush() can retry rather than silently no-op.
	Queue.Flush();
	TestEqual(TEXT("Retry after failure issues a new send"), Transport.GetSentCount(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueueSynchronousFailureCompletesOnceTest, "UnrealHog.Events.EventQueue.SynchronousSendStartFailureCompletesOnceAndAllowsRetry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueueSynchronousFailureCompletesOnceTest::RunTest(const FString& Parameters)
{
	FScopedQueueTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Storage(Fixture.GetRootPath());
	FPostHogFakeBatchTransport Transport;

	FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 100, 100, 2);

	Transport.SetSynchronousFailure(true);

	Queue.Enqueue(MakeTestEvent(TEXT("1")));
	Queue.Enqueue(MakeTestEvent(TEXT("2")));

	TestEqual(TEXT("Synchronous failure does not register a pending send"), Transport.GetSentCount(), 0);
	TestEqual(TEXT("Events remain queued after a synchronous send-start failure"), Queue.Num(), 2);

	// A stuck bIsFlushing would make this Flush() a silent no-op; it must actually retry.
	Transport.SetSynchronousFailure(false);
	Queue.Flush();
	TestEqual(TEXT("Queue is flushable again after a synchronous failure"), Transport.GetSentCount(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueueCancelPreventsLateCallbackTest, "UnrealHog.Events.EventQueue.CancelInFlightRequestPreventsLateCallbackFromMutatingQueue", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueueCancelPreventsLateCallbackTest::RunTest(const FString& Parameters)
{
	FScopedQueueTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Storage(Fixture.GetRootPath());
	FPostHogFakeBatchTransport Transport;

	FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 100, 100, 2);

	Queue.Enqueue(MakeTestEvent(TEXT("1")));
	Queue.Enqueue(MakeTestEvent(TEXT("2")));
	TestEqual(TEXT("Flush occurred"), Transport.GetSentCount(), 1);

	Queue.CancelInFlightRequest();
	TestTrue(TEXT("Fake handle observed the cancellation"), Transport.IsLastRequestCancelled());

	// If the cancellation guard were broken, a late success completion would clear the queue.
	Transport.CompleteLast(true, 200, TEXT(""));
	TestEqual(TEXT("Cancelled request's late callback did not mutate the queue"), Queue.Num(), 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueueDestructionCancelsInFlightTest, "UnrealHog.Events.EventQueue.DestroyingQueueMidFlightCancelsAndDoesNotCrash", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueueDestructionCancelsInFlightTest::RunTest(const FString& Parameters)
{
	FScopedQueueTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Storage(Fixture.GetRootPath());
	FPostHogFakeBatchTransport Transport;

	{
		FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 100, 100, 2);

		Queue.Enqueue(MakeTestEvent(TEXT("1")));
		Queue.Enqueue(MakeTestEvent(TEXT("2")));
		TestEqual(TEXT("Flush occurred"), Transport.GetSentCount(), 1);
	}
	// Queue destructor has run here, cancelling the in-flight request.

	TestTrue(TEXT("Destructor cancelled the pending request"), Transport.IsLastRequestCancelled());

	// A late callback firing after destruction must not crash or touch the destroyed queue.
	Transport.CompleteLast(true, 200, TEXT(""));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
