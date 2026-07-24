#include "Consent/PostHogConsentController.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/CoreDelegates.h"
#include "Events/PostHogBatchPayload.h"
#include "Lifecycle/PostHogQuitFlushCoordinator.h"
#include "Tests/PostHogAcceptanceFixture.h"

// EP-029: persistence, restart recovery, retry/backoff/adaptive-413/offline, and shutdown-drain
// paths, all exercised against durable state composed through FPostHogConsentController's public
// producer APIs. A standalone FPostHogEventQueue is constructed directly against the fixture's
// shared in-memory storage (never through the controller, which does not thread a clock or
// reachability provider into its internally-owned queue) to make retry timing and offline
// behavior deterministic -- see PostHogAcceptanceFixture.h's FNonOwningStorageProviderAdapter
// comment for why this is safe without any production-code changes.

namespace
{
	bool CoreIngressRecovery_TryGetSinglePayloadEventName(const FPostHogBatchPayload& Payload, FString& OutEventName)
	{
		const TSharedRef<FJsonObject> PayloadJson = Payload.ToJsonObject();

		const TArray<TSharedPtr<FJsonValue>>* BatchArray = nullptr;
		if (!PayloadJson->TryGetArrayField(TEXT("batch"), BatchArray) || BatchArray->Num() != 1)
		{
			return false;
		}

		const TSharedPtr<FJsonObject> EventObject = (*BatchArray)[0]->AsObject();
		return EventObject.IsValid() && EventObject->TryGetStringField(TEXT("event"), OutEventName);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogCoreIngressRestartRecoveryAndMultiBatchDrainTest, "UnrealHog.Acceptance.CoreIngress.RestartRecoveryAndMultiBatchDrain", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogCoreIngressRestartRecoveryAndMultiBatchDrainTest::RunTest(const FString& Parameters)
{
	FPostHogAcceptanceFixture Fixture;
	TUniquePtr<FPostHogConsentController> Controller = Fixture.MakeController();
	UPostHogDeveloperSettings* Settings = FPostHogAcceptanceFixture::MakeSettings(true, true, false, false);

	Controller->Initialize(*Settings);
	Controller->SetOptIn(true, *Settings);

	for (int32 Index = 1; Index <= 5; ++Index)
	{
		TestEqual(*FString::Printf(TEXT("Composed event %d succeeds"), Index),
			Controller->CaptureEvent(FString::Printf(TEXT("restart-event-%d"), Index), nullptr),
			EPostHogCaptureResult::Success);
	}
	TestEqual(TEXT("All composed events persisted before any flush"), Fixture.SharedStorage.GetEventCount(), 5);

	// Simulates closing the application: cancels in-flight work and drains storage, never
	// initiating network I/O; the controller object is not touched again in this test.
	Controller->Shutdown();

	FPostHogFakeBatchTransport RestartedTransport;
	TUniquePtr<FPostHogEventQueue> RestartedQueue = Fixture.MakeStandaloneQueue(Fixture.SharedStorage, RestartedTransport, TEXT("phc_valid_key"), 1000, 2, 100);

	TestEqual(TEXT("Restarted queue recovers the full storage-authoritative count"), RestartedQueue->Num(), 5);

	RestartedQueue->Flush();
	TestEqual(TEXT("First recovered batch sent"), RestartedTransport.GetTotalSendCount(), 1);
	TestEqual(TEXT("First recovered batch respects MaxBatchSize"), RestartedTransport.GetLastPayload().Num(), 2);

	RestartedTransport.CompleteLast(true, 200, TEXT(""));
	TestEqual(TEXT("Second recovered batch sent after first success"), RestartedTransport.GetTotalSendCount(), 2);
	TestEqual(TEXT("Second recovered batch respects MaxBatchSize"), RestartedTransport.GetLastPayload().Num(), 2);

	RestartedTransport.CompleteLast(true, 200, TEXT(""));
	TestEqual(TEXT("Third recovered batch sent after second success"), RestartedTransport.GetTotalSendCount(), 3);
	TestEqual(TEXT("Third recovered batch has the remainder"), RestartedTransport.GetLastPayload().Num(), 1);

	RestartedTransport.CompleteLast(true, 200, TEXT(""));
	TestEqual(TEXT("Restarted queue fully drained"), RestartedQueue->Num(), 0);
	TestEqual(TEXT("All recovered records removed from storage"), Fixture.SharedStorage.GetEventCount(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogCoreIngressCapacityEvictsOldestAcrossControllerTest, "UnrealHog.Acceptance.CoreIngress.CapacityEvictsOldestAcrossController", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogCoreIngressCapacityEvictsOldestAcrossControllerTest::RunTest(const FString& Parameters)
{
	FPostHogAcceptanceFixture Fixture;
	TUniquePtr<FPostHogConsentController> Controller = Fixture.MakeController();
	UPostHogDeveloperSettings* Settings = FPostHogAcceptanceFixture::MakeSettings(true, true, false, false, EPostHogPersonProfiles::IdentifiedOnly, /*MaxQueueSize*/ 1);

	Controller->Initialize(*Settings);
	Controller->SetOptIn(true, *Settings);

	TestEqual(TEXT("First capture succeeds"), Controller->CaptureEvent(TEXT("capacity-first"), nullptr), EPostHogCaptureResult::Success);
	TestEqual(TEXT("Second capture succeeds, evicting the first"), Controller->CaptureEvent(TEXT("capacity-second"), nullptr), EPostHogCaptureResult::Success);
	TestEqual(TEXT("Capacity of one keeps only the newest event"), Controller->GetQueuedEventCount(), 1);

	Controller->Flush();
	if (!TestNotNull(TEXT("Transport created"), Fixture.LastTransport))
	{
		return false;
	}

	FString SentEventName;
	TestTrue(TEXT("Sent payload has exactly one event"), CoreIngressRecovery_TryGetSinglePayloadEventName(Fixture.LastTransport->GetLastPayload(), SentEventName));
	Fixture.LastTransport->CompleteLast(true, 200, TEXT(""));

	TestEqual(TEXT("Surviving event is the second one, not the evicted first"), SentEventName, FString(TEXT("capacity-second")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogCoreIngressCorruptRecordSkippedWithoutBlockingDrainTest, "UnrealHog.Acceptance.CoreIngress.CorruptRecordSkippedWithoutBlockingDrain", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogCoreIngressCorruptRecordSkippedWithoutBlockingDrainTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("deleted corrupt persisted event"), EAutomationExpectedErrorFlags::Contains, 1);

	FPostHogAcceptanceFixture Fixture;
	TUniquePtr<FPostHogConsentController> Controller = Fixture.MakeController();
	UPostHogDeveloperSettings* Settings = FPostHogAcceptanceFixture::MakeSettings(true, true, false, false);

	Controller->Initialize(*Settings);
	Controller->SetOptIn(true, *Settings);

	TestEqual(TEXT("First (soon-corrupted) capture succeeds"), Controller->CaptureEvent(TEXT("corrupt-first"), nullptr), EPostHogCaptureResult::Success);
	const TArray<FString> IdsAfterFirst = Fixture.SharedStorage.GetEventIds();
	if (!TestEqual(TEXT("Exactly one record persisted so far"), IdsAfterFirst.Num(), 1))
	{
		return false;
	}
	const FString CorruptedEventId = IdsAfterFirst[0];

	TestEqual(TEXT("Second (valid) capture succeeds"), Controller->CaptureEvent(TEXT("corrupt-second-valid"), nullptr), EPostHogCaptureResult::Success);

	// Directly overwrite the first record's durable JSON with malformed content, simulating disk
	// corruption discovered on the next flush; never routed through CaptureEvent/Enqueue.
	Fixture.SharedStorage.SaveEvent(CorruptedEventId, TEXT("{not-json"));

	Controller->Shutdown();

	FPostHogFakeBatchTransport RecoveryTransport;
	TUniquePtr<FPostHogEventQueue> RecoveryQueue = Fixture.MakeStandaloneQueue(Fixture.SharedStorage, RecoveryTransport, TEXT("phc_valid_key"), 1000, 50, 100);
	RecoveryQueue->Flush();

	TestEqual(TEXT("One batch sent after skipping the corrupt record"), RecoveryTransport.GetSentCount(), 1);
	FString SentEventName;
	TestTrue(TEXT("Sent payload has exactly one event"), CoreIngressRecovery_TryGetSinglePayloadEventName(RecoveryTransport.GetLastPayload(), SentEventName));
	TestEqual(TEXT("The valid later record was sent, not the corrupt one"), SentEventName, FString(TEXT("corrupt-second-valid")));

	const TArray<FString> IdsAfterCorruptDelete = Fixture.SharedStorage.GetEventIds();
	TestFalse(TEXT("Corrupt record removed from storage"), IdsAfterCorruptDelete.Contains(CorruptedEventId));

	RecoveryTransport.CompleteLast(true, 200, TEXT(""));
	TestEqual(TEXT("Storage fully drained after the valid record sends"), Fixture.SharedStorage.GetEventCount(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogCoreIngressPermanentErrorRetryBackoffAndOfflineTest, "UnrealHog.Acceptance.CoreIngress.PermanentErrorRetryBackoffAndOffline", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogCoreIngressPermanentErrorRetryBackoffAndOfflineTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("classified batch delivery failure as permanent"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("received HTTP 413"), EAutomationExpectedErrorFlags::Contains, 1);

	FPostHogAcceptanceFixture Fixture;
	TUniquePtr<FPostHogConsentController> Controller = Fixture.MakeController();
	UPostHogDeveloperSettings* Settings = FPostHogAcceptanceFixture::MakeSettings(true, true, false, false);

	Controller->Initialize(*Settings);
	Controller->SetOptIn(true, *Settings);

	// Phase A: a permanent (400) failure deletes the attempted batch and never enters backoff.
	TestEqual(TEXT("Phase A capture succeeds"), Controller->CaptureEvent(TEXT("permanent-error-event"), nullptr), EPostHogCaptureResult::Success);
	{
		FPostHogFakeBatchTransport TransportA;
		TUniquePtr<FPostHogEventQueue> QueueA = Fixture.MakeStandaloneQueue(Fixture.SharedStorage, TransportA, TEXT("phc_valid_key"), 1000, 100, 100);

		QueueA->Flush();
		TransportA.CompleteLast(false, 400, TEXT(""));
		TestEqual(TEXT("Permanent failure deletes the attempted record"), Fixture.SharedStorage.GetEventCount(), 0);

		TOptional<EPostHogEventQueueFlushResult> ImmediateResult;
		QueueA->Flush([&ImmediateResult](EPostHogEventQueueFlushResult Result) { ImmediateResult = Result; });
		TestTrue(TEXT("Immediate flush after permanent failure completes"), ImmediateResult.IsSet());
		if (ImmediateResult.IsSet())
		{
			TestEqual(TEXT("Immediate flush after permanent failure is Empty, never Paused"), ImmediateResult.GetValue(), EPostHogEventQueueFlushResult::Empty);
		}
	}

	// Phase B: a retryable (500) failure enters a 5-second backoff, then succeeds on retry.
	TestEqual(TEXT("Phase B capture succeeds"), Controller->CaptureEvent(TEXT("retryable-error-event"), nullptr), EPostHogCaptureResult::Success);
	{
		FPostHogFakeBatchTransport TransportB;
		TUniquePtr<FPostHogEventQueue> QueueB = Fixture.MakeStandaloneQueue(Fixture.SharedStorage, TransportB, TEXT("phc_valid_key"), 1000, 100, 100);

		QueueB->Flush();
		TransportB.CompleteLast(false, 500, TEXT(""));

		TOptional<EPostHogEventQueueFlushResult> PausedResult;
		QueueB->Flush([&PausedResult](EPostHogEventQueueFlushResult Result) { PausedResult = Result; });
		TestTrue(TEXT("Immediate retry is paused"), PausedResult.IsSet());
		if (PausedResult.IsSet())
		{
			TestEqual(TEXT("Immediate retry result is Paused"), PausedResult.GetValue(), EPostHogEventQueueFlushResult::Paused);
		}
		TestEqual(TEXT("Paused retry issues no request"), TransportB.GetTotalSendCount(), 1);

		Fixture.Clock.Advance(FTimespan::FromSeconds(5) - FTimespan::FromMilliseconds(1));
		TOptional<EPostHogEventQueueFlushResult> StillPausedResult;
		QueueB->Flush([&StillPausedResult](EPostHogEventQueueFlushResult Result) { StillPausedResult = Result; });
		if (StillPausedResult.IsSet())
		{
			TestEqual(TEXT("Flush just before the 5s boundary is still Paused"), StillPausedResult.GetValue(), EPostHogEventQueueFlushResult::Paused);
		}

		Fixture.Clock.Advance(FTimespan::FromMilliseconds(1));
		QueueB->Flush();
		TestEqual(TEXT("Retry at the 5s boundary sends"), TransportB.GetTotalSendCount(), 2);
		TransportB.CompleteLast(true, 200, TEXT(""));
		TestEqual(TEXT("Retry success drains storage"), Fixture.SharedStorage.GetEventCount(), 0);
	}

	// Phase C: repeated 413 responses adaptively halve the batch/threshold limits, retaining
	// every event, until a smaller retry succeeds.
	for (int32 Index = 1; Index <= 6; ++Index)
	{
		TestEqual(*FString::Printf(TEXT("Phase C capture %d succeeds"), Index),
			Controller->CaptureEvent(FString::Printf(TEXT("adaptive-413-event-%d"), Index), nullptr),
			EPostHogCaptureResult::Success);
	}
	{
		FPostHogFakeBatchTransport TransportC;
		TUniquePtr<FPostHogEventQueue> QueueC = Fixture.MakeStandaloneQueue(Fixture.SharedStorage, TransportC, TEXT("phc_valid_key"), 1000, 4, 100);

		QueueC->Flush();
		TestEqual(TEXT("Initial oversized batch uses configured MaxBatchSize"), TransportC.GetLastPayload().Num(), 4);

		TransportC.CompleteLast(false, 413, TEXT(""));
		TestEqual(TEXT("413 retains every event"), Fixture.SharedStorage.GetEventCount(), 6);
		TestEqual(TEXT("413 halves the adjusted MaxBatchSize"), QueueC->GetAdjustedMaxBatchSizeForTests(), 2);

		Fixture.Clock.Advance(FTimespan::FromSeconds(5));
		QueueC->Flush();
		TestEqual(TEXT("Retry after 413 uses the halved batch size"), TransportC.GetLastPayload().Num(), 2);

		int32 SuccessfulBatches = 0;
		while (TransportC.GetPendingCount() > 0)
		{
			TransportC.CompleteLast(true, 200, TEXT(""));
			++SuccessfulBatches;
		}
		TestEqual(TEXT("Adjusted retry drains in three successful batches"), SuccessfulBatches, 3);
		TestEqual(TEXT("All adaptively-batched events delivered"), Fixture.SharedStorage.GetEventCount(), 0);
	}

	// Phase D: a known-offline flush is skipped without touching the transport, then drains once
	// reachability returns.
	TestEqual(TEXT("Phase D capture succeeds"), Controller->CaptureEvent(TEXT("offline-event"), nullptr), EPostHogCaptureResult::Success);
	{
		FPostHogFakeBatchTransport TransportD;
		TUniquePtr<FPostHogEventQueue> QueueD = Fixture.MakeStandaloneQueue(Fixture.SharedStorage, TransportD, TEXT("phc_valid_key"), 1000, 100, 100);

		Fixture.Reachability.SetState(EPostHogReachabilityState::NotReachable);
		TOptional<EPostHogEventQueueFlushResult> OfflineResult;
		QueueD->Flush([&OfflineResult](EPostHogEventQueueFlushResult Result) { OfflineResult = Result; });
		TestTrue(TEXT("Offline flush completes synchronously"), OfflineResult.IsSet());
		if (OfflineResult.IsSet())
		{
			TestEqual(TEXT("Offline flush result is SkippedOffline"), OfflineResult.GetValue(), EPostHogEventQueueFlushResult::SkippedOffline);
		}
		TestEqual(TEXT("Offline flush never touches the transport"), TransportD.GetTotalSendCount(), 0);
		TestEqual(TEXT("Offline flush preserves the queued event"), Fixture.SharedStorage.GetEventCount(), 1);

		Fixture.Reachability.SetState(EPostHogReachabilityState::Reachable);
		QueueD->Flush();
		TestEqual(TEXT("Reachable follow-up sends the preserved event"), TransportD.GetTotalSendCount(), 1);
		TransportD.CompleteLast(true, 200, TEXT(""));
		TestEqual(TEXT("Reachable follow-up drains storage"), Fixture.SharedStorage.GetEventCount(), 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogCoreIngressBackgroundFlushDrainsStorageTest, "UnrealHog.Acceptance.CoreIngress.BackgroundFlushDrainsStorage", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogCoreIngressBackgroundFlushDrainsStorageTest::RunTest(const FString& Parameters)
{
	FPostHogAcceptanceFixture Fixture;
	TUniquePtr<FPostHogConsentController> Controller = Fixture.MakeController();
	UPostHogDeveloperSettings* Settings = FPostHogAcceptanceFixture::MakeSettings(true, true, false, false);

	Controller->Initialize(*Settings);
	Controller->SetOptIn(true, *Settings);
	Controller->CaptureEvent(TEXT("background-event"), nullptr);

	if (!TestNotNull(TEXT("Transport created"), Fixture.LastTransport))
	{
		return false;
	}
	TestEqual(TEXT("One event queued before background"), Controller->GetQueuedEventCount(), 1);

	const int32 SendCountBeforeBackground = Fixture.LastTransport->GetTotalSendCount();
	const int32 DrainCountBeforeBackground = Fixture.SharedStorage.GetFlushPendingWritesCallCount();

	FCoreDelegates::ApplicationWillEnterBackgroundDelegate.Broadcast();

	TestEqual(TEXT("Background attempts a transport send"), Fixture.LastTransport->GetTotalSendCount(), SendCountBeforeBackground + 1);
	TestEqual(TEXT("Background synchronously drains storage"), Fixture.SharedStorage.GetFlushPendingWritesCallCount(), DrainCountBeforeBackground + 1);

	Fixture.LastTransport->CompleteLast(true, 200, TEXT(""));
	TestEqual(TEXT("Queue drained once the background flush completes"), Controller->GetQueuedEventCount(), 0);

	Controller->Shutdown();

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogCoreIngressBoundedQuitTimeoutDrainsStorageWithoutHangingTest, "UnrealHog.Acceptance.CoreIngress.BoundedQuitTimeoutDrainsStorageWithoutHanging", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogCoreIngressBoundedQuitTimeoutDrainsStorageWithoutHangingTest::RunTest(const FString& Parameters)
{
	FPostHogAcceptanceFixture Fixture;
	TUniquePtr<FPostHogConsentController> Controller = Fixture.MakeController();
	UPostHogDeveloperSettings* Settings = FPostHogAcceptanceFixture::MakeSettings(true, true, false, false);

	Controller->Initialize(*Settings);
	Controller->SetOptIn(true, *Settings);
	Controller->CaptureEvent(TEXT("quit-timeout-event"), nullptr);

	if (!TestNotNull(TEXT("Transport created"), Fixture.LastTransport))
	{
		return false;
	}

	int32 ScheduleTimeoutCallCount = 0;
	int32 RequestExitCallCount = 0;
	TFunction<void()> CapturedTimeoutCallback;

	FPostHogQuitFlushCoordinator Coordinator(
		[&Controller](FPostHogEventQueueFlushComplete OnComplete) { Controller->RequestFlush(MoveTemp(OnComplete)); },
		[&Controller]() { Controller->Shutdown(); },
		3.0f,
		[&RequestExitCallCount]() { ++RequestExitCallCount; },
		[&ScheduleTimeoutCallCount, &CapturedTimeoutCallback](float, TFunction<void()> OnTimeout) -> TFunction<void()>
		{
			++ScheduleTimeoutCallCount;
			CapturedTimeoutCallback = MoveTemp(OnTimeout);
			return []() {};
		});

	Coordinator.BeginFlushAndQuit();

	TestEqual(TEXT("Timeout scheduled once"), ScheduleTimeoutCallCount, 1);
	if (!TestTrue(TEXT("Timeout callback captured"), (bool)CapturedTimeoutCallback))
	{
		return false;
	}
	TestEqual(TEXT("RequestFlush started a network send"), Fixture.LastTransport->GetTotalSendCount(), 1);

	const int32 DrainCountBeforeTimeout = Fixture.SharedStorage.GetFlushPendingWritesCallCount();

	// The flush never completes in time; the bounded timeout must finalize without hanging.
	CapturedTimeoutCallback();

	TestTrue(TEXT("Coordinator finalized via the timeout"), Coordinator.IsFinalized());
	TestEqual(TEXT("RequestExit called exactly once"), RequestExitCallCount, 1);
	TestTrue(TEXT("Controller marked shutting down by the coordinator's ShutdownFunc"), Controller->IsShuttingDown());
	TestTrue(TEXT("Shutdown drains storage"), Fixture.SharedStorage.GetFlushPendingWritesCallCount() > DrainCountBeforeTimeout);
	TestEqual(TEXT("Timeout finalization issues no new network send"), Fixture.LastTransport->GetTotalSendCount(), 1);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
