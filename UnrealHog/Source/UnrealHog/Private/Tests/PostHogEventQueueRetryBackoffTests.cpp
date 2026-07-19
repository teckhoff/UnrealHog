#include "Events/PostHogEventQueue.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Events/PostHogEvent.h"
#include "SDK/PostHogSdkInfo.h"
#include "Storage/PostHogFileStorageProvider.h"
#include "Tests/PostHogFakeBatchTransport.h"
#include "Tests/PostHogFakeClock.h"
#include "Tests/PostHogFakeReachabilityProvider.h"

namespace
{
	// RAII fixture that owns a unique temporary directory for the file storage provider backing
	// these retry-backoff tests; removed on scope exit even if an assertion fails mid-test.
	class FScopedRetryBackoffTestStorageDirectory
	{
	public:
		FScopedRetryBackoffTestStorageDirectory()
			: RootPath(FPaths::Combine(FPlatformProcess::UserTempDir(), TEXT("UnrealHogTests"), FGuid::NewGuid().ToString(EGuidFormats::Digits)))
		{
		}

		~FScopedRetryBackoffTestStorageDirectory()
		{
			IFileManager::Get().DeleteDirectory(*RootPath, false, true);
		}

		const FString& GetRootPath() const { return RootPath; }

	private:
		FString RootPath;
	};

	FPostHogEvent MakeRetryTestEvent(const FString& Suffix)
	{
		return FPostHogEvent(FString::Printf(TEXT("retry-test-event-%s"), *Suffix), TEXT("distinct-id"));
	}

	EPostHogEventQueueFlushResult FlushAndCaptureResult(FPostHogEventQueue& Queue)
	{
		EPostHogEventQueueFlushResult CapturedResult = EPostHogEventQueueFlushResult::Empty;
		Queue.Flush([&CapturedResult](EPostHogEventQueueFlushResult Result) { CapturedResult = Result; });
		return CapturedResult;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueueRetryLinearBackoffDelaysMatchSequenceTest, "UnrealHog.Events.EventQueue.RetryLinearBackoffDelaysMatchSequence", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueueRetryLinearBackoffDelaysMatchSequenceTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("classified batch delivery failure as retryable; retaining attempted batch. StatusCode=500"), EAutomationExpectedErrorFlags::Contains, 7);

	FScopedRetryBackoffTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Storage(Fixture.GetRootPath());
	FPostHogFakeBatchTransport Transport;
	FPostHogFakeClock Clock;

	FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 100, 100, 1, &Clock);

	Queue.Enqueue(MakeRetryTestEvent(TEXT("1")));
	TestEqual(TEXT("Initial enqueue triggers a send"), Transport.GetSentCount(), 1);

	const TArray<int32> ExpectedDelaysSeconds = { 5, 10, 15, 20, 25, 30, 30 };

	for (int32 Round = 0; Round < ExpectedDelaysSeconds.Num(); ++Round)
	{
		// Fail the outstanding send (either the initial one, or the previous round's retry).
		Transport.CompleteLast(false, 500, TEXT(""));

		const int32 ExpectedDelaySeconds = ExpectedDelaysSeconds[Round];

		TestEqual(TEXT("Flush at pause start is paused"), static_cast<uint8>(FlushAndCaptureResult(Queue)), static_cast<uint8>(EPostHogEventQueueFlushResult::Paused));
		TestEqual(TEXT("Paused flush issues no send"), Transport.GetSentCount(), 0);

		Clock.Advance(FTimespan::FromSeconds(ExpectedDelaySeconds) - FTimespan::FromMilliseconds(1));
		TestEqual(TEXT("Flush just before boundary is still paused"), static_cast<uint8>(FlushAndCaptureResult(Queue)), static_cast<uint8>(EPostHogEventQueueFlushResult::Paused));
		TestEqual(TEXT("Still-paused flush issues no send"), Transport.GetSentCount(), 0);

		Clock.Advance(FTimespan::FromMilliseconds(1));
		Queue.Flush();
		TestEqual(TEXT("Retry at boundary issues a new send"), Transport.GetSentCount(), 1);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueueRetryBoundaryAtExactPauseInstantSendsTest, "UnrealHog.Events.EventQueue.RetryBoundaryAtExactPauseInstantSends", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueueRetryBoundaryAtExactPauseInstantSendsTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("classified batch delivery failure as retryable; retaining attempted batch. StatusCode=500"), EAutomationExpectedErrorFlags::Contains, 1);

	FScopedRetryBackoffTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Storage(Fixture.GetRootPath());
	FPostHogFakeBatchTransport Transport;
	FPostHogFakeClock Clock;

	FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 100, 100, 1, &Clock);

	Queue.Enqueue(MakeRetryTestEvent(TEXT("1")));
	TestEqual(TEXT("Initial send occurred"), Transport.GetSentCount(), 1);
	Transport.CompleteLast(false, 500, TEXT(""));

	Clock.Advance(FTimespan::FromSeconds(5));

	// At the exact pause instant, Unity's strict `<` comparison means the pause has elapsed.
	Queue.Flush();
	TestEqual(TEXT("Flush exactly at pause instant sends"), Transport.GetSentCount(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueueRetrySuccessResetsBackoffToFiveSecondsTest, "UnrealHog.Events.EventQueue.RetrySuccessResetsBackoffToFiveSeconds", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueueRetrySuccessResetsBackoffToFiveSecondsTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("classified batch delivery failure as retryable; retaining attempted batch. StatusCode=500"), EAutomationExpectedErrorFlags::Contains, 2);

	FScopedRetryBackoffTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Storage(Fixture.GetRootPath());
	FPostHogFakeBatchTransport Transport;
	FPostHogFakeClock Clock;

	FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 100, 100, 1, &Clock);

	// First retryable failure: 5s pause.
	Queue.Enqueue(MakeRetryTestEvent(TEXT("1")));
	Transport.CompleteLast(false, 500, TEXT(""));
	Clock.Advance(FTimespan::FromSeconds(5));

	// Retry succeeds, clearing the backoff state.
	Queue.Flush();
	TestEqual(TEXT("Retry after backoff sends"), Transport.GetSentCount(), 1);
	Transport.CompleteLast(true, 200, TEXT(""));
	TestEqual(TEXT("Success drains the queue"), Queue.Num(), 0);

	// A fresh retryable failure must again start its pause at 5 seconds, not cumulatively.
	Queue.Enqueue(MakeRetryTestEvent(TEXT("2")));
	TestEqual(TEXT("Fresh enqueue triggers a send"), Transport.GetSentCount(), 1);
	Transport.CompleteLast(false, 500, TEXT(""));

	TestEqual(TEXT("Immediate retry after fresh failure is paused"), static_cast<uint8>(FlushAndCaptureResult(Queue)), static_cast<uint8>(EPostHogEventQueueFlushResult::Paused));

	Clock.Advance(FTimespan::FromSeconds(5) - FTimespan::FromMilliseconds(1));
	TestEqual(TEXT("Flush just before 5s boundary is still paused"), static_cast<uint8>(FlushAndCaptureResult(Queue)), static_cast<uint8>(EPostHogEventQueueFlushResult::Paused));

	Clock.Advance(FTimespan::FromMilliseconds(1));
	Queue.Flush();
	TestEqual(TEXT("Retry at 5s boundary sends (not cumulative from earlier failure)"), Transport.GetSentCount(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueueRetryPermanentFailureDoesNotEnterBackoffTest, "UnrealHog.Events.EventQueue.RetryPermanentFailureDoesNotEnterBackoff", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueueRetryPermanentFailureDoesNotEnterBackoffTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("classified batch delivery failure as permanent; deleting attempted batch and ending flush. StatusCode=404"), EAutomationExpectedErrorFlags::Contains, 1);

	FScopedRetryBackoffTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Storage(Fixture.GetRootPath());
	FPostHogFakeBatchTransport Transport;
	FPostHogFakeClock Clock;

	FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 100, 100, 1, &Clock);

	Queue.Enqueue(MakeRetryTestEvent(TEXT("1")));
	TestEqual(TEXT("Initial send occurred"), Transport.GetSentCount(), 1);
	Transport.CompleteLast(false, 404, TEXT(""));

	// Permanent failures delete the attempted batch and drain the queue; nothing left to retry,
	// so an immediate flush must proceed (Empty), never Paused.
	TestEqual(TEXT("Queue drained after permanent failure"), Queue.Num(), 0);
	TestEqual(TEXT("Immediate flush after permanent failure is not paused"), static_cast<uint8>(FlushAndCaptureResult(Queue)), static_cast<uint8>(EPostHogEventQueueFlushResult::Empty));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueueRetryEnqueueDuringPauseDoesNotCreateHttpRequestTest, "UnrealHog.Events.EventQueue.RetryEnqueueDuringPauseDoesNotCreateHttpRequest", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueueRetryEnqueueDuringPauseDoesNotCreateHttpRequestTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("classified batch delivery failure as retryable; retaining attempted batch. StatusCode=500"), EAutomationExpectedErrorFlags::Contains, 1);

	FScopedRetryBackoffTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Storage(Fixture.GetRootPath());
	FPostHogFakeBatchTransport Transport;
	FPostHogFakeClock Clock;

	// FlushEventCount of 1 means every Enqueue() attempts a threshold-triggered Flush().
	FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 100, 100, 1, &Clock);

	Queue.Enqueue(MakeRetryTestEvent(TEXT("1")));
	TestEqual(TEXT("Initial send occurred"), Transport.GetSentCount(), 1);
	Transport.CompleteLast(false, 500, TEXT(""));

	const int32 TotalSendsBeforePause = Transport.GetTotalSendCount();

	// Threshold-triggered flushes during the pause window must not create HTTP requests.
	Queue.Enqueue(MakeRetryTestEvent(TEXT("2")));
	Queue.Enqueue(MakeRetryTestEvent(TEXT("3")));
	Queue.Enqueue(MakeRetryTestEvent(TEXT("4")));

	TestEqual(TEXT("No new sends while paused"), Transport.GetTotalSendCount(), TotalSendsBeforePause);
	TestEqual(TEXT("No pending send while paused"), Transport.GetSentCount(), 0);
	TestEqual(TEXT("Events remain queued during pause"), Queue.Num(), 4);

	Clock.Advance(FTimespan::FromSeconds(5));
	Queue.Flush();
	TestEqual(TEXT("Flush after pause elapses sends"), Transport.GetTotalSendCount(), TotalSendsBeforePause + 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueueOfflineSkipPreservesRetryBackoffTest, "UnrealHog.Events.EventQueue.OfflineSkipPreservesRetryBackoff", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueueOfflineSkipPreservesRetryBackoffTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("classified batch delivery failure as retryable; retaining attempted batch. StatusCode=500"), EAutomationExpectedErrorFlags::Contains, 1);

	FScopedRetryBackoffTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Storage(Fixture.GetRootPath());
	FPostHogFakeBatchTransport Transport;
	FPostHogFakeClock Clock;
	FPostHogFakeReachabilityProvider Reachability(EPostHogReachabilityState::Reachable);

	FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 100, 100, 1, &Clock, &Reachability);

	Queue.Enqueue(MakeRetryTestEvent(TEXT("1")));
	TestEqual(TEXT("Initial send occurred"), Transport.GetSentCount(), 1);
	Transport.CompleteLast(false, 500, TEXT(""));

	const int32 TotalSendsBeforeOfflineSkip = Transport.GetTotalSendCount();

	Reachability.SetState(EPostHogReachabilityState::NotReachable);
	TestEqual(TEXT("Offline flush is skipped"), static_cast<uint8>(FlushAndCaptureResult(Queue)), static_cast<uint8>(EPostHogEventQueueFlushResult::SkippedOffline));
	TestEqual(TEXT("Offline skip issues no send"), Transport.GetTotalSendCount(), TotalSendsBeforeOfflineSkip);
	TestEqual(TEXT("Offline skip leaves no pending request"), Transport.GetSentCount(), 0);

	Reachability.SetState(EPostHogReachabilityState::Reachable);
	TestEqual(TEXT("Reachable flush before pause expires remains paused"), static_cast<uint8>(FlushAndCaptureResult(Queue)), static_cast<uint8>(EPostHogEventQueueFlushResult::Paused));
	TestEqual(TEXT("Paused reachable flush issues no send"), Transport.GetTotalSendCount(), TotalSendsBeforeOfflineSkip);

	Clock.Advance(FTimespan::FromSeconds(5));
	Queue.Flush();
	TestEqual(TEXT("Reachable flush at pause boundary sends"), Transport.GetTotalSendCount(), TotalSendsBeforeOfflineSkip + 1);
	TestEqual(TEXT("Reachable flush creates one pending request"), Transport.GetSentCount(), 1);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
