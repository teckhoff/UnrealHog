#include "Events/PostHogEventQueue.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
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

	// Asserts the uuid, event, distinct_id, timestamp, and every flat string property in Actual
	// match Expected, so restart/mix tests can prove fields survived a persist/rehydrate round trip.
	void CheckEventJsonFieldsMatch(FAutomationTestBase& Test, const TSharedPtr<FJsonObject>& Actual, const TSharedRef<FJsonObject>& Expected, const FString& Context)
	{
		if (!Test.TestTrue(FString::Printf(TEXT("%s: actual event JSON is valid"), *Context), Actual.IsValid()))
		{
			return;
		}

		for (const TCHAR* Field : { TEXT("uuid"), TEXT("event"), TEXT("distinct_id"), TEXT("timestamp") })
		{
			FString ActualValue;
			FString ExpectedValue;
			Actual->TryGetStringField(Field, ActualValue);
			Expected->TryGetStringField(Field, ExpectedValue);
			Test.TestEqual(*FString::Printf(TEXT("%s: %s matches"), *Context, Field), ActualValue, ExpectedValue);
		}

		const TSharedPtr<FJsonObject>* ActualProperties = nullptr;
		const TSharedPtr<FJsonObject>* ExpectedProperties = nullptr;
		Actual->TryGetObjectField(TEXT("properties"), ActualProperties);
		Expected->TryGetObjectField(TEXT("properties"), ExpectedProperties);

		if (!Test.TestTrue(FString::Printf(TEXT("%s: both have a properties object"), *Context),
			ActualProperties != nullptr && ExpectedProperties != nullptr))
		{
			return;
		}

		Test.TestEqual(FString::Printf(TEXT("%s: properties field count matches"), *Context),
			(*ActualProperties)->Values.Num(), (*ExpectedProperties)->Values.Num());

		for (const auto& ExpectedPair : (*ExpectedProperties)->Values)
		{
			FString ActualPropertyValue;
			const bool bFound = (*ActualProperties)->TryGetStringField(ExpectedPair.Key, ActualPropertyValue);
			Test.TestTrue(FString::Printf(TEXT("%s: property \"%s\" present"), *Context, *ExpectedPair.Key), bFound);
			if (bFound)
			{
				Test.TestEqual(FString::Printf(TEXT("%s: property \"%s\" matches"), *Context, *ExpectedPair.Key),
					ActualPropertyValue, ExpectedPair.Value->AsString());
			}
		}
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueuePreSeededStorageTest, "UnrealHog.Events.EventQueue.PreSeededStorageIsSentWithoutNewCapture", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueuePreSeededStorageTest::RunTest(const FString& Parameters)
{
	FScopedQueueTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Storage(Fixture.GetRootPath());
	FPostHogFakeBatchTransport Transport;

	// Simulates events persisted by a prior run: written straight to storage, never through Enqueue.
	for (const TCHAR* Suffix : { TEXT("1"), TEXT("2"), TEXT("3") })
	{
		const FPostHogEvent PriorEvent = MakeTestEvent(Suffix);
		Storage.SaveEvent(PriorEvent.GetEventId(), PriorEvent.ToJsonObject());
	}
	Storage.FlushPendingWrites();

	TestEqual(TEXT("Storage already reports the pre-seeded events"), Storage.GetEventCount(), 3);

	FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 100, 100, 100);
	TestEqual(TEXT("A freshly constructed queue reports storage's pre-seeded count"), Queue.Num(), 3);

	Queue.Flush();
	TestEqual(TEXT("Flush sends the pre-seeded events without any new capture"), Transport.GetSentCount(), 1);
	TestEqual(TEXT("Batch contains all pre-seeded events"), Transport.GetLastPayload().Num(), 3);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueueSimulatedRestartTest, "UnrealHog.Events.EventQueue.SimulatedRestartRetainsOrderAndFields", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueueSimulatedRestartTest::RunTest(const FString& Parameters)
{
	FScopedQueueTestStorageDirectory Fixture;

	TArray<TSharedRef<FJsonObject>> ExpectedEventJson;

	{
		FPostHogFileStorageProvider Storage(Fixture.GetRootPath());
		FPostHogFakeBatchTransport Transport;
		FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 100, 100, 100);

		for (const TCHAR* Suffix : { TEXT("1"), TEXT("2"), TEXT("3") })
		{
			FPostHogEvent Event = MakeTestEvent(Suffix);
			Event.SetStringProperty(TEXT("marker"), FString::Printf(TEXT("marker-%s"), Suffix));

			ExpectedEventJson.Add(Event.ToJsonObject());
			TestTrue(TEXT("Enqueue succeeds"), Queue.Enqueue(Event));
		}

		TestEqual(TEXT("No flush was triggered before the simulated restart"), Transport.GetSentCount(), 0);
		// Queue and Storage are destroyed here (reverse declaration order), simulating a process restart;
		// FPostHogFileStorageProvider's destructor drains pending writes before the files disappear from scope.
	}

	FPostHogFileStorageProvider RestartedStorage(Fixture.GetRootPath());
	FPostHogFakeBatchTransport RestartedTransport;
	FPostHogEventQueue RestartedQueue(RestartedStorage, RestartedTransport, TEXT("test-api-key"), 100, 100, 100);

	TestEqual(TEXT("Restarted queue recovers the full pre-existing count"), RestartedQueue.Num(), 3);

	RestartedQueue.Flush();
	TestEqual(TEXT("Restarted queue sends the recovered events"), RestartedTransport.GetSentCount(), 1);

	const TSharedRef<FJsonObject> BatchPayloadJson = RestartedTransport.GetLastPayload().ToJsonObject();
	const TArray<TSharedPtr<FJsonValue>>* BatchArray = nullptr;
	TestTrue(TEXT("Payload has a batch array"), BatchPayloadJson->TryGetArrayField(TEXT("batch"), BatchArray));

	if (BatchArray)
	{
		TestEqual(TEXT("Recovered batch preserves event count"), BatchArray->Num(), ExpectedEventJson.Num());

		const int32 CompareCount = FMath::Min(BatchArray->Num(), ExpectedEventJson.Num());
		for (int32 Index = 0; Index < CompareCount; ++Index)
		{
			CheckEventJsonFieldsMatch(*this, (*BatchArray)[Index]->AsObject(), ExpectedEventJson[Index],
				FString::Printf(TEXT("Event %d"), Index));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueueAsyncWriteVisibleTest, "UnrealHog.Events.EventQueue.AsyncWriteVisibleForThresholdAndCount", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueueAsyncWriteVisibleTest::RunTest(const FString& Parameters)
{
	FScopedQueueTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Storage(Fixture.GetRootPath());
	FPostHogFakeBatchTransport Transport;

	FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 100, 100, 3);

	Queue.Enqueue(MakeTestEvent(TEXT("1")));
	TestEqual(TEXT("Storage count reflects the write immediately"), Storage.GetEventCount(), 1);
	TestEqual(TEXT("Queue count reflects the write immediately"), Queue.Num(), 1);
	TestEqual(TEXT("No flush below FlushEventCount"), Transport.GetSentCount(), 0);

	Queue.Enqueue(MakeTestEvent(TEXT("2")));
	TestEqual(TEXT("Storage count still tracks synchronously"), Storage.GetEventCount(), 2);
	TestEqual(TEXT("Queue count still tracks synchronously"), Queue.Num(), 2);
	TestEqual(TEXT("Still no flush below FlushEventCount"), Transport.GetSentCount(), 0);

	Queue.Enqueue(MakeTestEvent(TEXT("3")));
	TestEqual(TEXT("Storage count reaches the threshold"), Storage.GetEventCount(), 3);
	TestEqual(TEXT("Crossing FlushEventCount triggers a flush"), Transport.GetSentCount(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueueCancellationLeavesRecordsTest, "UnrealHog.Events.EventQueue.CancellationLeavesRecordsForNextInstance", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueueCancellationLeavesRecordsTest::RunTest(const FString& Parameters)
{
	FScopedQueueTestStorageDirectory Fixture;

	{
		FPostHogFileStorageProvider Storage(Fixture.GetRootPath());
		FPostHogFakeBatchTransport Transport;
		FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 100, 100, 2);

		Queue.Enqueue(MakeTestEvent(TEXT("1")));
		Queue.Enqueue(MakeTestEvent(TEXT("2")));
		TestEqual(TEXT("Flush occurred"), Transport.GetSentCount(), 1);

		Queue.CancelInFlightRequest();
		TestEqual(TEXT("Cancellation leaves the persisted records intact"), Storage.GetEventCount(), 2);
		// Queue and Storage are destroyed here; the cancelled records must still be on disk afterward.
	}

	FPostHogFileStorageProvider NextStorage(Fixture.GetRootPath());
	FPostHogFakeBatchTransport NextTransport;
	FPostHogEventQueue NextQueue(NextStorage, NextTransport, TEXT("test-api-key"), 100, 100, 2);

	TestEqual(TEXT("Next instance sees the cancelled records"), NextQueue.Num(), 2);

	NextQueue.Flush();
	TestEqual(TEXT("Next instance can flush the recovered records"), NextTransport.GetSentCount(), 1);
	TestEqual(TEXT("Next instance's batch contains both records"), NextTransport.GetLastPayload().Num(), 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueueCurrentAndPriorRunMixTest, "UnrealHog.Events.EventQueue.CurrentAndPriorRunMix", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueueCurrentAndPriorRunMixTest::RunTest(const FString& Parameters)
{
	FScopedQueueTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Storage(Fixture.GetRootPath());
	FPostHogFakeBatchTransport Transport;

	// Simulates a record left behind by a prior run, persisted before this queue instance exists.
	const FPostHogEvent PriorRunEvent = MakeTestEvent(TEXT("prior"));
	Storage.SaveEvent(PriorRunEvent.GetEventId(), PriorRunEvent.ToJsonObject());
	Storage.FlushPendingWrites();

	FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 100, 100, 100);

	const FPostHogEvent CurrentRunEvent = MakeTestEvent(TEXT("current"));
	TestTrue(TEXT("Enqueue succeeds for the current-run event"), Queue.Enqueue(CurrentRunEvent));

	TestEqual(TEXT("Queue counts both the prior-run and current-run records"), Queue.Num(), 2);

	const TArray<FString> ExpectedOrder = Storage.GetEventIds();
	TestEqual(TEXT("Deterministic storage order covers both records"), ExpectedOrder.Num(), 2);

	Queue.Flush();
	TestEqual(TEXT("Flush sends both records in a single batch"), Transport.GetSentCount(), 1);
	TestEqual(TEXT("Batch contains both records"), Transport.GetLastPayload().Num(), 2);

	const TSharedRef<FJsonObject> BatchPayloadJson = Transport.GetLastPayload().ToJsonObject();
	const TArray<TSharedPtr<FJsonValue>>* BatchArray = nullptr;
	TestTrue(TEXT("Payload has a batch array"), BatchPayloadJson->TryGetArrayField(TEXT("batch"), BatchArray));

	if (BatchArray && BatchArray->Num() == ExpectedOrder.Num())
	{
		for (int32 Index = 0; Index < ExpectedOrder.Num(); ++Index)
		{
			FString ActualUuid;
			(*BatchArray)[Index]->AsObject()->TryGetStringField(TEXT("uuid"), ActualUuid);
			TestEqual(*FString::Printf(TEXT("Batch entry %d matches deterministic storage ID order"), Index), ActualUuid, ExpectedOrder[Index]);
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
