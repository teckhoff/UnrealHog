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
#include "SDK/PostHogSdkInfo.h"
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

		FString GetQueueDirectory() const
		{
			return FPaths::Combine(RootPath, FPostHogSdkInfo::GetLibraryName(), TEXT("Queue"));
		}

	private:
		FString RootPath;
	};

	FPostHogEvent MakeTestEvent(const FString& Suffix)
	{
		return FPostHogEvent(FString::Printf(TEXT("test-event-%s"), *Suffix), TEXT("distinct-id"));
	}

	FString MakePersistedEventJson(const FString& EventId)
	{
		return FString::Printf(TEXT("{\"uuid\":\"%s\",\"event\":\"seeded-event\",\"distinct_id\":\"distinct-id\",\"timestamp\":\"2026-07-16T00:00:00.000Z\",\"properties\":{}}"), *EventId);
	}

	const FString SeedEventId1 = TEXT("00000000-0000-7000-8000-000000000001");
	const FString SeedEventId2 = TEXT("00000000-0000-7000-8000-000000000002");
	const FString SeedEventId3 = TEXT("00000000-0000-7000-8000-000000000003");
	const FString CorruptFirstEventId = TEXT("00000000-0000-3000-8000-000000000001");
	const FString LegacyUuidV4EventId = TEXT("00000000-0000-4000-8000-000000000002");

	class FControllableQueueStorageProvider final : public IPostHogStorageProvider
	{
	public:
		int32 DeleteAttempts = 0;
		int32 LoadAttempts = 0;
		int32 SaveAttempts = 0;
		FString LastDeletedEventId;
		FString LastSavedEventId;
		TMap<FString, int32> LoadAttemptsById;

		void SeedEvent(const FString& EventId)
		{
			SeedEvent(EventId, MakePersistedEventJson(EventId));
		}

		void SeedEvent(const FString& EventId, const FString& EventJson)
		{
			Events.Add(EventId, EventJson);
			EventIdIndex.AddUnique(EventId);
			EventIdIndex.Sort();
		}

		void SeedIndexedMissingEvent(const FString& EventId)
		{
			Events.Remove(EventId);
			EventIdIndex.AddUnique(EventId);
			EventIdIndex.Sort();
		}

		void SetFailNextDelete(bool bFail)
		{
			bFailNextDelete = bFail;
		}

		void SetFailNextSave(bool bFail)
		{
			bFailNextSave = bFail;
		}

		virtual bool SaveEvent(const FString& EventId, const FString& EventJson) override
		{
			++SaveAttempts;
			LastSavedEventId = EventId;

			if (bFailNextSave)
			{
				bFailNextSave = false;
				return false;
			}

			SeedEvent(EventId, EventJson);
			return true;
		}
		using IPostHogStorageProvider::SaveEvent;

		virtual bool LoadEvent(const FString& EventId, FString& EventJson) override
		{
			++LoadAttempts;
			int32& AttemptsForId = LoadAttemptsById.FindOrAdd(EventId);
			++AttemptsForId;

			const FString* Found = Events.Find(EventId);
			if (!Found)
			{
				EventJson.Empty();
				return false;
			}

			EventJson = *Found;
			return true;
		}

		virtual bool DeleteEvent(const FString& EventId) override
		{
			++DeleteAttempts;
			LastDeletedEventId = EventId;

			if (bFailNextDelete)
			{
				bFailNextDelete = false;
				return false;
			}

			const bool bWasIndexed = EventIdIndex.Remove(EventId) > 0;
			const bool bHadEvent = Events.Remove(EventId) > 0;
			return bWasIndexed || bHadEvent;
		}

		virtual bool ClearEvents() override
		{
			Events.Empty();
			EventIdIndex.Empty();
			return true;
		}

		virtual TArray<FString> GetEventIds() override
		{
			return EventIdIndex;
		}

		virtual int32 GetEventCount() override
		{
			return EventIdIndex.Num();
		}

		virtual bool SaveState(const FString& StateKey, const FString& StateJson) override
		{
			State.Add(StateKey, StateJson);
			return true;
		}
		using IPostHogStorageProvider::SaveState;

		virtual bool LoadState(const FString& StateKey, FString& StateJson) override
		{
			const FString* Found = State.Find(StateKey);
			if (!Found)
			{
				StateJson.Empty();
				return false;
			}

			StateJson = *Found;
			return true;
		}

		virtual bool DeleteState(const FString& StateKey) override
		{
			return State.Remove(StateKey) > 0;
		}

	private:
		TMap<FString, FString> Events;
		TMap<FString, FString> State;
		TArray<FString> EventIdIndex;
		bool bFailNextDelete = false;
		bool bFailNextSave = false;
	};

	void CheckEventIds(FAutomationTestBase& Test, FControllableQueueStorageProvider& Storage, const TArray<FString>& ExpectedIds, const FString& Context)
	{
		TArray<FString> SortedExpectedIds = ExpectedIds;
		SortedExpectedIds.Sort();

		const TArray<FString> ActualIds = Storage.GetEventIds();
		Test.TestEqual(*FString::Printf(TEXT("%s: event count"), *Context), ActualIds.Num(), SortedExpectedIds.Num());

		const int32 CompareCount = FMath::Min(ActualIds.Num(), SortedExpectedIds.Num());
		for (int32 Index = 0; Index < CompareCount; ++Index)
		{
			Test.TestEqual(*FString::Printf(TEXT("%s: id %d"), *Context, Index), ActualIds[Index], SortedExpectedIds[Index]);
		}
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueueCorruptFirstRecordDeletedAndLaterRecordsFillBatchTest, "UnrealHog.Events.EventQueue.CorruptFirstRecordDeletedAndLaterRecordsFillBatch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueueCorruptFirstRecordDeletedAndLaterRecordsFillBatchTest::RunTest(const FString& Parameters)
{
	FControllableQueueStorageProvider Storage;
	FPostHogFakeBatchTransport Transport;

	Storage.SeedEvent(CorruptFirstEventId, TEXT("{not-json"));
	Storage.SeedEvent(LegacyUuidV4EventId, MakePersistedEventJson(LegacyUuidV4EventId));
	Storage.SeedEvent(SeedEventId3, MakePersistedEventJson(SeedEventId3));

	FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 100, 2, 100);
	Queue.Flush();

	TestEqual(TEXT("One corrupt record delete attempted"), Storage.DeleteAttempts, 1);
	TestEqual(TEXT("Malformed first record deleted"), Storage.LastDeletedEventId, CorruptFirstEventId);
	TestEqual(TEXT("One batch sent after deleting corrupt first record"), Transport.GetSentCount(), 1);
	TestEqual(TEXT("Batch filled with two later valid records"), Transport.GetLastPayload().Num(), 2);

	const TSharedRef<FJsonObject> BatchPayloadJson = Transport.GetLastPayload().ToJsonObject();
	const TArray<TSharedPtr<FJsonValue>>* BatchArray = nullptr;
	TestTrue(TEXT("Payload has a batch array"), BatchPayloadJson->TryGetArrayField(TEXT("batch"), BatchArray));

	if (BatchArray && BatchArray->Num() == 2)
	{
		FString FirstUuid;
		FString SecondUuid;
		(*BatchArray)[0]->AsObject()->TryGetStringField(TEXT("uuid"), FirstUuid);
		(*BatchArray)[1]->AsObject()->TryGetStringField(TEXT("uuid"), SecondUuid);

		TestEqual(TEXT("First valid payload keeps legacy UUIDv4"), FirstUuid, LegacyUuidV4EventId);
		TestEqual(TEXT("Second valid payload sent after legacy record"), SecondUuid, SeedEventId3);
	}

	TestEqual(TEXT("Only valid in-flight records remain before success"), Queue.Num(), 2);
	Transport.CompleteLast(true, 200, TEXT(""));
	TestEqual(TEXT("Successful completion empties remaining queue"), Queue.Num(), 0);
	CheckEventIds(*this, Storage, {}, TEXT("Corrupt first success final storage"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueueMissingFileDeletedAndLaterRecordSentTest, "UnrealHog.Events.EventQueue.MissingFileDeletedAndLaterRecordSent", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueueMissingFileDeletedAndLaterRecordSentTest::RunTest(const FString& Parameters)
{
	FScopedQueueTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Storage(Fixture.GetRootPath());
	FPostHogFakeBatchTransport Transport;

	TestTrue(TEXT("First save succeeds"), Storage.SaveEvent(SeedEventId1, MakePersistedEventJson(SeedEventId1)));
	TestTrue(TEXT("Second save succeeds"), Storage.SaveEvent(SeedEventId2, MakePersistedEventJson(SeedEventId2)));
	Storage.FlushPendingWrites();

	const FString MissingEventPath = FPaths::Combine(Fixture.GetQueueDirectory(), FString::Printf(TEXT("%s.json"), *SeedEventId1));
	TestTrue(TEXT("Seeded first event file deleted directly"), IFileManager::Get().Delete(*MissingEventPath, false, true));

	FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 100, 1, 100);
	Queue.Flush();

	TestEqual(TEXT("Missing file does not block later event send"), Transport.GetSentCount(), 1);
	TestEqual(TEXT("Batch contains later valid event"), Transport.GetLastPayload().Num(), 1);

	const TArray<FString> EventIdsAfterFlush = Storage.GetEventIds();
	TestFalse(TEXT("Missing ID removed from provider index"), EventIdsAfterFlush.Contains(SeedEventId1));
	TestTrue(TEXT("Later valid ID remains in flight"), EventIdsAfterFlush.Contains(SeedEventId2));

	const TSharedRef<FJsonObject> BatchPayloadJson = Transport.GetLastPayload().ToJsonObject();
	const TArray<TSharedPtr<FJsonValue>>* BatchArray = nullptr;
	TestTrue(TEXT("Payload has a batch array"), BatchPayloadJson->TryGetArrayField(TEXT("batch"), BatchArray));

	if (BatchArray && BatchArray->Num() == 1)
	{
		FString SentUuid;
		(*BatchArray)[0]->AsObject()->TryGetStringField(TEXT("uuid"), SentUuid);
		TestEqual(TEXT("Later valid event was sent"), SentUuid, SeedEventId2);
	}

	Transport.CompleteLast(true, 200, TEXT(""));
	TestEqual(TEXT("Successful completion empties queue"), Queue.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueueCorruptDeleteFailureStopsFlushWithoutRepeatedReadTest, "UnrealHog.Events.EventQueue.CorruptDeleteFailureStopsFlushWithoutRepeatedRead", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueueCorruptDeleteFailureStopsFlushWithoutRepeatedReadTest::RunTest(const FString& Parameters)
{
	FControllableQueueStorageProvider Storage;
	FPostHogFakeBatchTransport Transport;

	Storage.SeedEvent(CorruptFirstEventId, FString::Printf(TEXT("{\"uuid\":\"%s\",\"event\":\"seeded-event\",\"distinct_id\":\"distinct-id\",\"timestamp\":\"2026-07-16T00:00:00.000Z\",\"properties\":\"not-an-object\"}"), *CorruptFirstEventId));
	Storage.SeedEvent(SeedEventId3, MakePersistedEventJson(SeedEventId3));
	Storage.SetFailNextDelete(true);

	FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 100, 2, 100);
	Queue.Flush();

	TestEqual(TEXT("No batch sent when corrupt delete fails"), Transport.GetSentCount(), 0);
	TestEqual(TEXT("Exactly one corrupt delete attempted"), Storage.DeleteAttempts, 1);
	TestEqual(TEXT("Failed delete targeted corrupt first record"), Storage.LastDeletedEventId, CorruptFirstEventId);
	TestEqual(TEXT("Only corrupt record was loaded"), Storage.LoadAttempts, 1);

	const int32* CorruptLoadAttempts = Storage.LoadAttemptsById.Find(CorruptFirstEventId);
	TestTrue(TEXT("Corrupt first record load was recorded"), CorruptLoadAttempts != nullptr);
	if (CorruptLoadAttempts)
	{
		TestEqual(TEXT("Corrupt first record read once"), *CorruptLoadAttempts, 1);
	}

	TestFalse(TEXT("Later valid record was not read after delete failure"), Storage.LoadAttemptsById.Contains(SeedEventId3));
	CheckEventIds(*this, Storage, { CorruptFirstEventId, SeedEventId3 }, TEXT("Delete failure final storage"));

	return true;
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
			TestEqual(TEXT("Enqueue succeeds"), Queue.Enqueue(Event), EPostHogEventQueueEnqueueResult::Enqueued);
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
	TestEqual(TEXT("Enqueue succeeds for the current-run event"), Queue.Enqueue(CurrentRunEvent), EPostHogEventQueueEnqueueResult::Enqueued);

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueueCapacityOneEvictsOldestBeforeSaveTest, "UnrealHog.Events.EventQueue.CapacityOneEvictsOldestBeforeSave", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueueCapacityOneEvictsOldestBeforeSaveTest::RunTest(const FString& Parameters)
{
	FControllableQueueStorageProvider Storage;
	FPostHogFakeBatchTransport Transport;
	Storage.SeedEvent(SeedEventId1);

	FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 1, 100, 100);

	const FPostHogEvent IncomingEvent = MakeTestEvent(TEXT("incoming"));
	const FString IncomingEventId = IncomingEvent.GetEventId();
	const EPostHogEventQueueEnqueueResult Result = Queue.Enqueue(IncomingEvent);

	TestEqual(TEXT("Enqueue succeeds after evicting oldest"), Result, EPostHogEventQueueEnqueueResult::Enqueued);
	TestEqual(TEXT("One delete attempted"), Storage.DeleteAttempts, 1);
	TestEqual(TEXT("Oldest prior record deleted"), Storage.LastDeletedEventId, SeedEventId1);
	TestEqual(TEXT("Incoming record saved once"), Storage.SaveAttempts, 1);
	TestEqual(TEXT("Incoming event was saved"), Storage.LastSavedEventId, IncomingEventId);
	TestEqual(TEXT("Queue remains capped at one record"), Queue.Num(), 1);
	CheckEventIds(*this, Storage, { IncomingEventId }, TEXT("Capacity one final storage"));
	TestEqual(TEXT("No flush triggered by capacity eviction"), Transport.GetSentCount(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueueRestartedStorageCountsTowardCapacityTest, "UnrealHog.Events.EventQueue.RestartedStorageCountsTowardCapacity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueueRestartedStorageCountsTowardCapacityTest::RunTest(const FString& Parameters)
{
	FControllableQueueStorageProvider Storage;
	FPostHogFakeBatchTransport Transport;
	Storage.SeedEvent(SeedEventId1);
	Storage.SeedEvent(SeedEventId2);

	FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 2, 100, 100);

	const FPostHogEvent IncomingEvent = MakeTestEvent(TEXT("incoming"));
	const FString IncomingEventId = IncomingEvent.GetEventId();
	const EPostHogEventQueueEnqueueResult Result = Queue.Enqueue(IncomingEvent);

	TestEqual(TEXT("Restarted storage capacity enqueue succeeds"), Result, EPostHogEventQueueEnqueueResult::Enqueued);
	TestEqual(TEXT("Preexisting count caused one eviction"), Storage.DeleteAttempts, 1);
	TestEqual(TEXT("Oldest preexisting id evicted"), Storage.LastDeletedEventId, SeedEventId1);
	TestEqual(TEXT("Incoming record saved once"), Storage.SaveAttempts, 1);
	TestEqual(TEXT("Queue remains at configured capacity"), Queue.Num(), 2);
	CheckEventIds(*this, Storage, { SeedEventId2, IncomingEventId }, TEXT("Restarted capacity final storage"));
	TestEqual(TEXT("No flush triggered by restarted capacity eviction"), Transport.GetSentCount(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueueMultipleInFlightSkippedDuringEvictionTest, "UnrealHog.Events.EventQueue.MultipleInFlightIdsAreSkippedDuringEviction", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueueMultipleInFlightSkippedDuringEvictionTest::RunTest(const FString& Parameters)
{
	FControllableQueueStorageProvider Storage;
	FPostHogFakeBatchTransport Transport;
	Storage.SeedEvent(SeedEventId1);
	Storage.SeedEvent(SeedEventId2);
	Storage.SeedEvent(SeedEventId3);

	FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 3, 2, 100);
	Queue.Flush();

	TestEqual(TEXT("Initial flush sent the two oldest records"), Transport.GetSentCount(), 1);
	TestEqual(TEXT("Initial batch uses MaxBatchSize"), Transport.GetLastPayload().Num(), 2);

	const FPostHogEvent IncomingEvent = MakeTestEvent(TEXT("incoming"));
	const FString IncomingEventId = IncomingEvent.GetEventId();
	const EPostHogEventQueueEnqueueResult Result = Queue.Enqueue(IncomingEvent);

	TestEqual(TEXT("Enqueue succeeds by evicting first non-in-flight id"), Result, EPostHogEventQueueEnqueueResult::Enqueued);
	TestEqual(TEXT("One delete attempted"), Storage.DeleteAttempts, 1);
	TestEqual(TEXT("Newest eligible record deleted after skipping in-flight ids"), Storage.LastDeletedEventId, SeedEventId3);
	TestEqual(TEXT("Incoming record saved once"), Storage.SaveAttempts, 1);
	TestEqual(TEXT("Queue remains at configured capacity"), Queue.Num(), 3);
	CheckEventIds(*this, Storage, { SeedEventId1, SeedEventId2, IncomingEventId }, TEXT("In-flight skip final storage"));
	TestEqual(TEXT("No additional flush while request is in flight"), Transport.GetSentCount(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueueAllInFlightAtCapacityRejectsNewEventTest, "UnrealHog.Events.EventQueue.AllInFlightAtCapacityRejectsNewEvent", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueueAllInFlightAtCapacityRejectsNewEventTest::RunTest(const FString& Parameters)
{
	FControllableQueueStorageProvider Storage;
	FPostHogFakeBatchTransport Transport;
	Storage.SeedEvent(SeedEventId1);
	Storage.SeedEvent(SeedEventId2);

	FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 2, 2, 100);
	Queue.Flush();

	const FPostHogEvent IncomingEvent = MakeTestEvent(TEXT("incoming"));
	const EPostHogEventQueueEnqueueResult Result = Queue.Enqueue(IncomingEvent);

	TestEqual(TEXT("All in-flight records reject new enqueue"), Result, EPostHogEventQueueEnqueueResult::RejectedCapacityNoEvictableEvent);
	TestEqual(TEXT("No delete attempted when all ids are in flight"), Storage.DeleteAttempts, 0);
	TestEqual(TEXT("Incoming record was not saved"), Storage.SaveAttempts, 0);
	TestEqual(TEXT("Persisted count unchanged"), Queue.Num(), 2);
	CheckEventIds(*this, Storage, { SeedEventId1, SeedEventId2 }, TEXT("All in-flight final storage"));
	TestEqual(TEXT("Only initial in-flight send exists"), Transport.GetSentCount(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueueDeleteFailureRejectsAndPreservesOldRecordTest, "UnrealHog.Events.EventQueue.DeleteFailureRejectsAndPreservesOldRecord", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueueDeleteFailureRejectsAndPreservesOldRecordTest::RunTest(const FString& Parameters)
{
	FControllableQueueStorageProvider Storage;
	FPostHogFakeBatchTransport Transport;
	Storage.SeedEvent(SeedEventId1);
	Storage.SetFailNextDelete(true);

	FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 1, 100, 100);

	const FPostHogEvent IncomingEvent = MakeTestEvent(TEXT("incoming"));
	const EPostHogEventQueueEnqueueResult Result = Queue.Enqueue(IncomingEvent);

	TestEqual(TEXT("Delete failure rejects new enqueue"), Result, EPostHogEventQueueEnqueueResult::RejectedCapacityDeleteFailed);
	TestEqual(TEXT("One delete attempted"), Storage.DeleteAttempts, 1);
	TestEqual(TEXT("Oldest id was the failed delete target"), Storage.LastDeletedEventId, SeedEventId1);
	TestEqual(TEXT("Save not attempted after delete failure"), Storage.SaveAttempts, 0);
	TestEqual(TEXT("Persisted count unchanged"), Queue.Num(), 1);
	CheckEventIds(*this, Storage, { SeedEventId1 }, TEXT("Delete failure final storage"));
	TestEqual(TEXT("No flush triggered after delete failure"), Transport.GetSentCount(), 0);

	FString PreservedJson;
	TestTrue(TEXT("Failed delete leaves old record loadable"), Storage.LoadEvent(SeedEventId1, PreservedJson));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueueSaveFailureRejectedAndConsistentCountTest, "UnrealHog.Events.EventQueue.SaveFailureReturnsRejectedSaveFailedAndLeavesConsistentCount", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueueSaveFailureRejectedAndConsistentCountTest::RunTest(const FString& Parameters)
{
	FControllableQueueStorageProvider Storage;
	FPostHogFakeBatchTransport Transport;
	Storage.SeedEvent(SeedEventId1);
	Storage.SetFailNextSave(true);

	FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 1, 100, 100);

	const FPostHogEvent IncomingEvent = MakeTestEvent(TEXT("incoming"));
	const FString IncomingEventId = IncomingEvent.GetEventId();
	const EPostHogEventQueueEnqueueResult Result = Queue.Enqueue(IncomingEvent);

	TestEqual(TEXT("Save failure returns rejected save result"), Result, EPostHogEventQueueEnqueueResult::RejectedSaveFailed);
	TestEqual(TEXT("Capacity eviction happened before save attempt"), Storage.DeleteAttempts, 1);
	TestEqual(TEXT("Old record was evicted before save failed"), Storage.LastDeletedEventId, SeedEventId1);
	TestEqual(TEXT("Save attempted once"), Storage.SaveAttempts, 1);
	TestEqual(TEXT("Incoming event was the failed save target"), Storage.LastSavedEventId, IncomingEventId);
	TestEqual(TEXT("Provider-visible count matches stored records after save failure"), Queue.Num(), 0);
	CheckEventIds(*this, Storage, {}, TEXT("Save failure final storage"));
	TestEqual(TEXT("No flush triggered after save failure"), Transport.GetSentCount(), 0);

	FString MissingJson;
	TestFalse(TEXT("Incoming failed save is not loadable"), Storage.LoadEvent(IncomingEventId, MissingJson));
	TestFalse(TEXT("Old record already evicted before failed save"), Storage.LoadEvent(SeedEventId1, MissingJson));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
