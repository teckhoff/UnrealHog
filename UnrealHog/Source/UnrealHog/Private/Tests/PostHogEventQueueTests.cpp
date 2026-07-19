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
#include "PostHogDeveloperSettings.h"
#include "SDK/PostHogSdkInfo.h"
#include "Storage/PostHogFileStorageProvider.h"
#include "Tests/PostHogFakeBatchTransport.h"
#include "Tests/PostHogFakeClock.h"
#include "Tests/PostHogFakeReachabilityProvider.h"
#include "Tests/PostHogTestPropertyHelpers.h"
#include "UObject/Package.h"

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

	FString MakeNumberedSeedEventId(int32 Index)
	{
		return FString::Printf(TEXT("00000000-0000-7000-8000-%012d"), Index);
	}

	UPostHogDeveloperSettings* MakeTransientQueueSettings(int32 MaxBatchSize, int32 FlushEventCount)
	{
		UPostHogDeveloperSettings* Settings = NewObject<UPostHogDeveloperSettings>(GetTransientPackage());
		UnrealHogTests::SetPropertyValue<FString>(Settings, TEXT("ApiKey"), TEXT("phc_valid_key"));
		UnrealHogTests::SetPropertyValue<int32>(Settings, TEXT("MaxBatchSize"), MaxBatchSize);
		UnrealHogTests::SetPropertyValue<int32>(Settings, TEXT("FlushEventCount"), FlushEventCount);
		return Settings;
	}

	EPostHogEventQueueFlushResult FlushQueueAndCaptureResult(FPostHogEventQueue& Queue)
	{
		TOptional<EPostHogEventQueueFlushResult> Result;
		Queue.Flush([&Result](EPostHogEventQueueFlushResult InResult)
		{
			Result = InResult;
		});
		check(Result.IsSet());
		return Result.GetValue();
	}

	const FString SeedEventId1 = TEXT("00000000-0000-7000-8000-000000000001");
	const FString SeedEventId2 = TEXT("00000000-0000-7000-8000-000000000002");
	const FString SeedEventId3 = TEXT("00000000-0000-7000-8000-000000000003");
	const FString SeedEventId4 = TEXT("00000000-0000-7000-8000-000000000004");
	const FString SeedEventId5 = TEXT("00000000-0000-7000-8000-000000000005");
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

	TArray<FString> SeedNumberedEvents(FControllableQueueStorageProvider& Storage, int32 Count)
	{
		TArray<FString> EventIds;
		EventIds.Reserve(Count);

		for (int32 Index = 1; Index <= Count; ++Index)
		{
			const FString EventId = MakeNumberedSeedEventId(Index);
			Storage.SeedEvent(EventId);
			EventIds.Add(EventId);
		}

		return EventIds;
	}

	TArray<FString> CopyEventIdRange(const TArray<FString>& EventIds, int32 StartIndex, int32 Count)
	{
		TArray<FString> Range;
		Range.Reserve(Count);

		for (int32 Index = 0; Index < Count && EventIds.IsValidIndex(StartIndex + Index); ++Index)
		{
			Range.Add(EventIds[StartIndex + Index]);
		}

		return Range;
	}

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

	TArray<FString> ExtractPayloadUuids(const FPostHogBatchPayload& Payload)
	{
		TArray<FString> Uuids;
		const TSharedRef<FJsonObject> PayloadJson = Payload.ToJsonObject();
		const TArray<TSharedPtr<FJsonValue>>* BatchArray = nullptr;
		if (!PayloadJson->TryGetArrayField(TEXT("batch"), BatchArray) || BatchArray == nullptr)
		{
			return Uuids;
		}

		Uuids.Reserve(BatchArray->Num());
		for (const TSharedPtr<FJsonValue>& BatchValue : *BatchArray)
		{
			FString Uuid;
			TSharedPtr<FJsonObject> EventObject;
			if (BatchValue.IsValid())
			{
				EventObject = BatchValue->AsObject();
			}
			if (EventObject.IsValid())
			{
				EventObject->TryGetStringField(TEXT("uuid"), Uuid);
			}
			Uuids.Add(Uuid);
		}

		return Uuids;
	}

	void CheckPayloadUuids(FAutomationTestBase& Test, const FPostHogBatchPayload& Payload, const TArray<FString>& ExpectedUuids, const FString& Context)
	{
		const TArray<FString> ActualUuids = ExtractPayloadUuids(Payload);
		Test.TestEqual(*FString::Printf(TEXT("%s: uuid count"), *Context), ActualUuids.Num(), ExpectedUuids.Num());

		const int32 CompareCount = FMath::Min(ActualUuids.Num(), ExpectedUuids.Num());
		for (int32 Index = 0; Index < CompareCount; ++Index)
		{
			Test.TestEqual(*FString::Printf(TEXT("%s: uuid %d"), *Context, Index), ActualUuids[Index], ExpectedUuids[Index]);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueueKnownOfflineFlushSkipsTransportAndPreservesQueueTest, "UnrealHog.Events.EventQueue.KnownOfflineFlushSkipsTransportAndPreservesQueue", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueueKnownOfflineFlushSkipsTransportAndPreservesQueueTest::RunTest(const FString& Parameters)
{
	FControllableQueueStorageProvider Storage;
	FPostHogFakeBatchTransport Transport;
	FPostHogFakeReachabilityProvider Reachability(EPostHogReachabilityState::NotReachable);
	TOptional<EPostHogEventQueueFlushResult> Result;

	Storage.SeedEvent(SeedEventId1);
	Storage.SeedEvent(SeedEventId2);

	const TArray<FString> ExpectedIds = Storage.GetEventIds();
	FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 100, 2, 100, nullptr, &Reachability);
	Queue.Flush([&Result](EPostHogEventQueueFlushResult InResult)
	{
		Result = InResult;
	});

	TestTrue(TEXT("Known-offline flush completed synchronously"), Result.IsSet());
	if (Result.IsSet())
	{
		TestEqual(TEXT("Known-offline flush result"), Result.GetValue(), EPostHogEventQueueFlushResult::SkippedOffline);
	}
	TestEqual(TEXT("Known-offline flush creates no transport request"), Transport.GetTotalSendCount(), 0);
	TestEqual(TEXT("Known-offline flush loads no event"), Storage.LoadAttempts, 0);
	TestEqual(TEXT("Known-offline flush deletes no event"), Storage.DeleteAttempts, 0);
	CheckEventIds(*this, Storage, ExpectedIds, TEXT("Known-offline flush preserves queue"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueueReachableFlushSendsBatchTest, "UnrealHog.Events.EventQueue.ReachableFlushSendsBatch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueueReachableFlushSendsBatchTest::RunTest(const FString& Parameters)
{
	FControllableQueueStorageProvider Storage;
	FPostHogFakeBatchTransport Transport;
	FPostHogFakeReachabilityProvider Reachability(EPostHogReachabilityState::Reachable);
	TOptional<EPostHogEventQueueFlushResult> Result;

	Storage.SeedEvent(SeedEventId1);

	FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 100, 2, 100, nullptr, &Reachability);
	Queue.Flush([&Result](EPostHogEventQueueFlushResult InResult)
	{
		Result = InResult;
	});

	TestEqual(TEXT("Reachable flush sends one request"), Transport.GetTotalSendCount(), 1);
	TestEqual(TEXT("Reachable flush loads one event"), Storage.LoadAttempts, 1);
	TestFalse(TEXT("Reachable flush waits for request completion"), Result.IsSet());

	Transport.CompleteLast(true, 200, TEXT(""));

	TestEqual(TEXT("Reachable success deletes sent event"), Storage.DeleteAttempts, 1);
	TestEqual(TEXT("Reachable success drains queue"), Queue.Num(), 0);
	TestTrue(TEXT("Reachable flush completed"), Result.IsSet());
	if (Result.IsSet())
	{
		TestEqual(TEXT("Reachable flush result"), Result.GetValue(), EPostHogEventQueueFlushResult::Drained);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueueUnknownFlushSendsBatchTest, "UnrealHog.Events.EventQueue.UnknownFlushSendsBatch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueueUnknownFlushSendsBatchTest::RunTest(const FString& Parameters)
{
	FControllableQueueStorageProvider Storage;
	FPostHogFakeBatchTransport Transport;
	FPostHogFakeReachabilityProvider Reachability(EPostHogReachabilityState::Unknown);
	TOptional<EPostHogEventQueueFlushResult> Result;

	Storage.SeedEvent(SeedEventId1);

	FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 100, 2, 100, nullptr, &Reachability);
	Queue.Flush([&Result](EPostHogEventQueueFlushResult InResult)
	{
		Result = InResult;
	});

	TestEqual(TEXT("Unknown flush sends one request"), Transport.GetTotalSendCount(), 1);
	TestEqual(TEXT("Unknown flush loads one event"), Storage.LoadAttempts, 1);
	TestFalse(TEXT("Unknown flush waits for request completion"), Result.IsSet());

	Transport.CompleteLast(true, 200, TEXT(""));

	TestEqual(TEXT("Unknown success deletes sent event"), Storage.DeleteAttempts, 1);
	TestEqual(TEXT("Unknown success drains queue"), Queue.Num(), 0);
	TestTrue(TEXT("Unknown flush completed"), Result.IsSet());
	if (Result.IsSet())
	{
		TestEqual(TEXT("Unknown flush result"), Result.GetValue(), EPostHogEventQueueFlushResult::Drained);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueueOfflineToReachableNextFlushDrainsTest, "UnrealHog.Events.EventQueue.OfflineToReachableNextFlushDrains", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueueOfflineToReachableNextFlushDrainsTest::RunTest(const FString& Parameters)
{
	FControllableQueueStorageProvider Storage;
	FPostHogFakeBatchTransport Transport;
	FPostHogFakeReachabilityProvider Reachability(EPostHogReachabilityState::NotReachable);
	TOptional<EPostHogEventQueueFlushResult> ReachableResult;

	Storage.SeedEvent(SeedEventId1);

	FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 100, 2, 100, nullptr, &Reachability);

	const EPostHogEventQueueFlushResult OfflineResult = FlushQueueAndCaptureResult(Queue);
	TestEqual(TEXT("Offline flush is skipped"), OfflineResult, EPostHogEventQueueFlushResult::SkippedOffline);
	TestEqual(TEXT("Offline flush sends no request"), Transport.GetTotalSendCount(), 0);
	TestEqual(TEXT("Offline flush preserves queued event"), Queue.Num(), 1);

	Reachability.SetState(EPostHogReachabilityState::Reachable);
	Queue.Flush([&ReachableResult](EPostHogEventQueueFlushResult InResult)
	{
		ReachableResult = InResult;
	});

	TestEqual(TEXT("Reachable follow-up sends existing queued event"), Transport.GetTotalSendCount(), 1);
	CheckPayloadUuids(*this, Transport.GetPayloadAt(0), { SeedEventId1 }, TEXT("Offline-to-reachable batch"));
	TestFalse(TEXT("Reachable follow-up waits for request completion"), ReachableResult.IsSet());

	Transport.CompleteLast(true, 200, TEXT(""));

	TestEqual(TEXT("Reachable follow-up drains queue"), Queue.Num(), 0);
	TestTrue(TEXT("Reachable follow-up completed"), ReachableResult.IsSet());
	if (ReachableResult.IsSet())
	{
		TestEqual(TEXT("Reachable follow-up result"), ReachableResult.GetValue(), EPostHogEventQueueFlushResult::Drained);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueueEmptyFlushCompletesWithoutRequestTest, "UnrealHog.Events.EventQueue.EmptyFlushCompletesWithoutRequest", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueueEmptyFlushCompletesWithoutRequestTest::RunTest(const FString& Parameters)
{
	FControllableQueueStorageProvider Storage;
	FPostHogFakeBatchTransport Transport;
	TOptional<EPostHogEventQueueFlushResult> Result;

	FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 100, 2, 100);
	Queue.Flush([&Result](EPostHogEventQueueFlushResult InResult)
	{
		Result = InResult;
	});

	TestEqual(TEXT("No request created for empty flush"), Transport.GetTotalSendCount(), 0);
	TestTrue(TEXT("Empty flush completed synchronously"), Result.IsSet());
	if (Result.IsSet())
	{
		TestEqual(TEXT("Empty flush result"), Result.GetValue(), EPostHogEventQueueFlushResult::Empty);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueueExactBoundarySingleBatchCompletesTest, "UnrealHog.Events.EventQueue.ExactBoundarySingleBatchCompletes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueueExactBoundarySingleBatchCompletesTest::RunTest(const FString& Parameters)
{
	FControllableQueueStorageProvider Storage;
	FPostHogFakeBatchTransport Transport;
	TOptional<EPostHogEventQueueFlushResult> Result;

	Storage.SeedEvent(SeedEventId1);
	Storage.SeedEvent(SeedEventId2);

	FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 100, 2, 100);
	Queue.Flush([&Result](EPostHogEventQueueFlushResult InResult)
	{
		Result = InResult;
	});

	TestEqual(TEXT("Exactly one pending request"), Transport.GetPendingCount(), 1);
	TestEqual(TEXT("Exactly one request sent"), Transport.GetTotalSendCount(), 1);
	CheckPayloadUuids(*this, Transport.GetPayloadAt(0), { SeedEventId1, SeedEventId2 }, TEXT("Exact boundary batch"));
	TestEqual(TEXT("No delete before request success"), Storage.DeleteAttempts, 0);
	TestFalse(TEXT("Flush callback waits for request completion"), Result.IsSet());

	Transport.CompleteLast(true, 200, TEXT(""));

	TestEqual(TEXT("No pending request after exact-boundary drain"), Transport.GetPendingCount(), 0);
	TestEqual(TEXT("No extra request after exact-boundary drain"), Transport.GetTotalSendCount(), 1);
	TestEqual(TEXT("Successful send deletes both records"), Storage.DeleteAttempts, 2);
	TestEqual(TEXT("Queue drained"), Queue.Num(), 0);
	TestTrue(TEXT("Flush completed"), Result.IsSet());
	if (Result.IsSet())
	{
		TestEqual(TEXT("Flush result"), Result.GetValue(), EPostHogEventQueueFlushResult::Drained);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueueFlushDrainsMultipleBatchesInOrderTest, "UnrealHog.Events.EventQueue.FlushDrainsMultipleBatchesInOrder", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueueFlushDrainsMultipleBatchesInOrderTest::RunTest(const FString& Parameters)
{
	FControllableQueueStorageProvider Storage;
	FPostHogFakeBatchTransport Transport;
	TOptional<EPostHogEventQueueFlushResult> Result;

	Storage.SeedEvent(SeedEventId1);
	Storage.SeedEvent(SeedEventId2);
	Storage.SeedEvent(SeedEventId3);
	Storage.SeedEvent(SeedEventId4);
	Storage.SeedEvent(SeedEventId5);

	FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 100, 2, 100);
	Queue.Flush([&Result](EPostHogEventQueueFlushResult InResult)
	{
		Result = InResult;
	});

	TestEqual(TEXT("Only first batch pending"), Transport.GetPendingCount(), 1);
	TestEqual(TEXT("First batch sent"), Transport.GetTotalSendCount(), 1);
	CheckPayloadUuids(*this, Transport.GetPayloadAt(0), { SeedEventId1, SeedEventId2 }, TEXT("First batch"));
	TestEqual(TEXT("No delete before first success"), Storage.DeleteAttempts, 0);

	Transport.CompleteLast(true, 200, TEXT(""));
	TestEqual(TEXT("Only second batch pending"), Transport.GetPendingCount(), 1);
	TestEqual(TEXT("Second batch sent after first success"), Transport.GetTotalSendCount(), 2);
	CheckPayloadUuids(*this, Transport.GetPayloadAt(1), { SeedEventId3, SeedEventId4 }, TEXT("Second batch"));
	TestEqual(TEXT("First batch deleted after success"), Storage.DeleteAttempts, 2);
	TestFalse(TEXT("Flush still active after first success"), Result.IsSet());

	Transport.CompleteLast(true, 200, TEXT(""));
	TestEqual(TEXT("Only third batch pending"), Transport.GetPendingCount(), 1);
	TestEqual(TEXT("Third batch sent after second success"), Transport.GetTotalSendCount(), 3);
	CheckPayloadUuids(*this, Transport.GetPayloadAt(2), { SeedEventId5 }, TEXT("Third batch"));
	TestEqual(TEXT("Second batch deleted after success"), Storage.DeleteAttempts, 4);

	Transport.CompleteLast(true, 200, TEXT(""));
	TestEqual(TEXT("No pending requests after drain"), Transport.GetPendingCount(), 0);
	TestEqual(TEXT("No duplicate requests"), Transport.GetTotalSendCount(), 3);
	TestEqual(TEXT("All records deleted after success"), Storage.DeleteAttempts, 5);
	TestEqual(TEXT("Queue empty after drain"), Queue.Num(), 0);
	CheckEventIds(*this, Storage, {}, TEXT("Multi-batch final storage"));
	TestTrue(TEXT("Flush completed"), Result.IsSet());
	if (Result.IsSet())
	{
		TestEqual(TEXT("Flush result"), Result.GetValue(), EPostHogEventQueueFlushResult::Drained);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueueAlreadyFlushingCoalescesCompletionWithoutSecondSendTest, "UnrealHog.Events.EventQueue.AlreadyFlushingCoalescesCompletionWithoutSecondSend", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueueAlreadyFlushingCoalescesCompletionWithoutSecondSendTest::RunTest(const FString& Parameters)
{
	FControllableQueueStorageProvider Storage;
	FPostHogFakeBatchTransport Transport;
	TOptional<EPostHogEventQueueFlushResult> FirstResult;
	TOptional<EPostHogEventQueueFlushResult> SecondResult;

	Storage.SeedEvent(SeedEventId1);
	Storage.SeedEvent(SeedEventId2);
	Storage.SeedEvent(SeedEventId3);

	FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 100, 2, 100);
	Queue.Flush([&FirstResult](EPostHogEventQueueFlushResult InResult)
	{
		FirstResult = InResult;
	});
	Queue.Flush([&SecondResult](EPostHogEventQueueFlushResult InResult)
	{
		SecondResult = InResult;
	});

	TestEqual(TEXT("Concurrent flush creates no second pending request"), Transport.GetPendingCount(), 1);
	TestEqual(TEXT("Concurrent flush creates no extra send"), Transport.GetTotalSendCount(), 1);
	TestFalse(TEXT("First callback pending while request active"), FirstResult.IsSet());
	TestFalse(TEXT("Second callback pending while request active"), SecondResult.IsSet());

	Transport.CompleteLast(true, 200, TEXT(""));
	TestEqual(TEXT("Second batch scheduled after first success"), Transport.GetPendingCount(), 1);
	TestEqual(TEXT("Second batch is the only additional send"), Transport.GetTotalSendCount(), 2);

	Transport.CompleteLast(true, 200, TEXT(""));
	TestEqual(TEXT("Flush fully drained"), Queue.Num(), 0);
	TestEqual(TEXT("No pending requests after coalesced drain"), Transport.GetPendingCount(), 0);
	TestTrue(TEXT("First callback completed"), FirstResult.IsSet());
	TestTrue(TEXT("Second callback completed"), SecondResult.IsSet());
	if (FirstResult.IsSet())
	{
		TestEqual(TEXT("First callback result"), FirstResult.GetValue(), EPostHogEventQueueFlushResult::Drained);
	}
	if (SecondResult.IsSet())
	{
		TestEqual(TEXT("Second callback result"), SecondResult.GetValue(), EPostHogEventQueueFlushResult::Drained);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueueFailureStopsBeforeLaterBatchAndPreservesRecordsTest, "UnrealHog.Events.EventQueue.FailureStopsBeforeLaterBatchAndPreservesRecords", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueueFailureStopsBeforeLaterBatchAndPreservesRecordsTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("classified batch delivery failure as retryable; retaining attempted batch. StatusCode=500"), EAutomationExpectedErrorFlags::Contains, 1);

	FControllableQueueStorageProvider Storage;
	FPostHogFakeBatchTransport Transport;
	TOptional<EPostHogEventQueueFlushResult> Result;

	Storage.SeedEvent(SeedEventId1);
	Storage.SeedEvent(SeedEventId2);
	Storage.SeedEvent(SeedEventId3);

	FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 100, 2, 100);
	Queue.Flush([&Result](EPostHogEventQueueFlushResult InResult)
	{
		Result = InResult;
	});

	CheckPayloadUuids(*this, Transport.GetPayloadAt(0), { SeedEventId1, SeedEventId2 }, TEXT("Failed first batch"));
	Transport.CompleteLast(false, 500, TEXT(""));

	TestEqual(TEXT("Failed request sends no later batch"), Transport.GetTotalSendCount(), 1);
	TestEqual(TEXT("No pending request after failure"), Transport.GetPendingCount(), 0);
	TestEqual(TEXT("Failed request deletes no records"), Storage.DeleteAttempts, 0);
	CheckEventIds(*this, Storage, { SeedEventId1, SeedEventId2, SeedEventId3 }, TEXT("Failure final storage"));
	TestTrue(TEXT("Failure result completed"), Result.IsSet());
	if (Result.IsSet())
	{
		TestEqual(TEXT("Failure result"), Result.GetValue(), EPostHogEventQueueFlushResult::Failed);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueuePermanentFailureDeletesOnlyAttemptedBatchAndEndsFlushTest, "UnrealHog.Events.EventQueue.PermanentFailureDeletesOnlyAttemptedBatchAndEndsFlush", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueuePermanentFailureDeletesOnlyAttemptedBatchAndEndsFlushTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("classified batch delivery failure as permanent; deleting attempted batch and ending flush. StatusCode=404"), EAutomationExpectedErrorFlags::Contains, 1);

	FControllableQueueStorageProvider Storage;
	FPostHogFakeBatchTransport Transport;
	TOptional<EPostHogEventQueueFlushResult> Result;

	Storage.SeedEvent(SeedEventId1);
	Storage.SeedEvent(SeedEventId2);
	Storage.SeedEvent(SeedEventId3);

	FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 100, 2, 100);
	Queue.Flush([&Result](EPostHogEventQueueFlushResult InResult)
	{
		Result = InResult;
	});

	CheckPayloadUuids(*this, Transport.GetPayloadAt(0), { SeedEventId1, SeedEventId2 }, TEXT("Permanent failure first batch"));
	Transport.CompleteLast(false, 404, TEXT(""));

	TestEqual(TEXT("Permanent failure sends no later batch"), Transport.GetTotalSendCount(), 1);
	TestEqual(TEXT("No pending request after permanent failure"), Transport.GetPendingCount(), 0);
	TestEqual(TEXT("Permanent failure deletes only the attempted batch"), Storage.DeleteAttempts, 2);
	CheckEventIds(*this, Storage, { SeedEventId3 }, TEXT("Permanent failure final storage"));
	TestTrue(TEXT("Permanent failure result completed"), Result.IsSet());
	if (Result.IsSet())
	{
		TestEqual(TEXT("Permanent failure result"), Result.GetValue(), EPostHogEventQueueFlushResult::Failed);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueueClassifiesDeliveryFailuresTest, "UnrealHog.Events.EventQueue.ClassifiesDeliveryFailuresAsPermanentOrRetryable", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueueClassifiesDeliveryFailuresTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("classified batch delivery failure as retryable"), EAutomationExpectedErrorFlags::Contains, 11);
	AddExpectedError(TEXT("classified batch delivery failure as permanent"), EAutomationExpectedErrorFlags::Contains, 5);
	AddExpectedError(TEXT("received HTTP 413"), EAutomationExpectedErrorFlags::Contains, 1);

	struct FStatusClassificationRow
	{
		int32 StatusCode;
		bool bExpectPermanent;
		const TCHAR* Label;
	};

	const FStatusClassificationRow Rows[] = {
		{ 0, false, TEXT("Status0") },
		{ 100, false, TEXT("Status100") },
		{ 200, false, TEXT("Status200") },
		{ 204, false, TEXT("Status204") },
		{ 301, false, TEXT("Status301") },
		{ 302, false, TEXT("Status302") },
		{ 308, false, TEXT("Status308") },
		{ 399, false, TEXT("Status399") },
		{ 400, true, TEXT("Status400") },
		{ 401, true, TEXT("Status401") },
		{ 404, true, TEXT("Status404") },
		{ 413, false, TEXT("Status413") },
		{ 429, true, TEXT("Status429") },
		{ 499, true, TEXT("Status499") },
		{ 500, false, TEXT("Status500") },
		{ 599, false, TEXT("Status599") },
	};

	for (const FStatusClassificationRow& Row : Rows)
	{
		FControllableQueueStorageProvider Storage;
		FPostHogFakeBatchTransport Transport;
		TOptional<EPostHogEventQueueFlushResult> Result;

		Storage.SeedEvent(SeedEventId1);
		Storage.SeedEvent(SeedEventId2);
		Storage.SeedEvent(SeedEventId3);

		FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 100, 2, 100);
		Queue.Flush([&Result](EPostHogEventQueueFlushResult InResult)
		{
			Result = InResult;
		});

		Transport.CompleteLast(false, Row.StatusCode, TEXT(""));

		TestTrue(*FString::Printf(TEXT("%s: result set"), Row.Label), Result.IsSet());
		if (Result.IsSet())
		{
			TestEqual(*FString::Printf(TEXT("%s: result is Failed"), Row.Label), Result.GetValue(), EPostHogEventQueueFlushResult::Failed);
		}

		if (Row.bExpectPermanent)
		{
			TestEqual(*FString::Printf(TEXT("%s: permanent failure deletes attempted batch"), Row.Label), Storage.DeleteAttempts, 2);
			CheckEventIds(*this, Storage, { SeedEventId3 }, FString::Printf(TEXT("%s: permanent failure storage"), Row.Label));
			TestEqual(*FString::Printf(TEXT("%s: permanent failure ends flush without a later batch"), Row.Label), Transport.GetTotalSendCount(), 1);
		}
		else
		{
			TestEqual(*FString::Printf(TEXT("%s: retryable failure deletes nothing"), Row.Label), Storage.DeleteAttempts, 0);
			CheckEventIds(*this, Storage, { SeedEventId1, SeedEventId2, SeedEventId3 }, FString::Printf(TEXT("%s: retryable failure storage"), Row.Label));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueuePayloadTooLargeHalvesAdjustedLimitsTest, "UnrealHog.Events.EventQueue.PayloadTooLargeHalvesAdjustedLimits", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueuePayloadTooLargeHalvesAdjustedLimitsTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("received HTTP 413"), EAutomationExpectedErrorFlags::Contains, 2);
	AddExpectedError(TEXT("classified batch delivery failure as retryable; retaining attempted batch. StatusCode=413"), EAutomationExpectedErrorFlags::Contains, 2);

	FControllableQueueStorageProvider Storage;
	FPostHogFakeBatchTransport Transport;
	FPostHogFakeClock Clock;
	UPostHogDeveloperSettings* Settings = MakeTransientQueueSettings(50, 20);
	const TArray<FString> EventIds = SeedNumberedEvents(Storage, 60);

	FPostHogEventQueue Queue(Storage, Transport, Settings->GetApiKey(), 100, Settings->GetMaxBatchSize(), Settings->GetFlushEventCount(), &Clock);
	TestEqual(TEXT("Initial adjusted MaxBatchSize comes from settings"), Queue.GetAdjustedMaxBatchSizeForTests(), 50);
	TestEqual(TEXT("Initial adjusted FlushEventCount comes from settings"), Queue.GetAdjustedFlushEventCountForTests(), 20);

	TOptional<EPostHogEventQueueFlushResult> FirstResult;
	Queue.Flush([&FirstResult](EPostHogEventQueueFlushResult Result)
	{
		FirstResult = Result;
	});
	TestEqual(TEXT("Initial flush sends one batch"), Transport.GetTotalSendCount(), 1);
	TestEqual(TEXT("Initial batch uses configured MaxBatchSize"), Transport.GetPayloadAt(0).Num(), 50);
	CheckPayloadUuids(*this, Transport.GetPayloadAt(0), CopyEventIdRange(EventIds, 0, 50), TEXT("Initial oversized batch"));

	Transport.CompleteLast(false, 413, TEXT(""));
	TestEqual(TEXT("First 413 retains every event"), Queue.Num(), 60);
	TestEqual(TEXT("First 413 deletes no records"), Storage.DeleteAttempts, 0);
	TestTrue(TEXT("First 413 completes the flush"), FirstResult.IsSet());
	if (FirstResult.IsSet())
	{
		TestEqual(TEXT("First 413 result"), FirstResult.GetValue(), EPostHogEventQueueFlushResult::Failed);
	}
	TestEqual(TEXT("First 413 halves adjusted MaxBatchSize"), Queue.GetAdjustedMaxBatchSizeForTests(), 25);
	TestEqual(TEXT("First 413 halves adjusted FlushEventCount"), Queue.GetAdjustedFlushEventCountForTests(), 10);
	TestEqual(TEXT("Settings MaxBatchSize stays unchanged"), Settings->GetMaxBatchSize(), 50);
	TestEqual(TEXT("Settings FlushEventCount stays unchanged"), Settings->GetFlushEventCount(), 20);

	const EPostHogEventQueueFlushResult ImmediateRetryResult = FlushQueueAndCaptureResult(Queue);
	TestEqual(TEXT("Immediate retry after first 413 is paused"), ImmediateRetryResult, EPostHogEventQueueFlushResult::Paused);
	TestEqual(TEXT("Paused retry sends no request"), Transport.GetTotalSendCount(), 1);

	Clock.Advance(FTimespan::FromSeconds(5));
	TOptional<EPostHogEventQueueFlushResult> SecondResult;
	Queue.Flush([&SecondResult](EPostHogEventQueueFlushResult Result)
	{
		SecondResult = Result;
	});
	TestEqual(TEXT("Retry after first backoff sends one more batch"), Transport.GetTotalSendCount(), 2);
	TestEqual(TEXT("Retry batch uses reduced MaxBatchSize"), Transport.GetPayloadAt(1).Num(), 25);
	CheckPayloadUuids(*this, Transport.GetPayloadAt(1), CopyEventIdRange(EventIds, 0, 25), TEXT("First reduced retry batch"));

	Transport.CompleteLast(false, 413, TEXT(""));
	TestEqual(TEXT("Second 413 still retains every event"), Queue.Num(), 60);
	TestEqual(TEXT("Second 413 still deletes no records"), Storage.DeleteAttempts, 0);
	TestTrue(TEXT("Second 413 completes the flush"), SecondResult.IsSet());
	if (SecondResult.IsSet())
	{
		TestEqual(TEXT("Second 413 result"), SecondResult.GetValue(), EPostHogEventQueueFlushResult::Failed);
	}
	TestEqual(TEXT("Second 413 halves adjusted MaxBatchSize with integer floor"), Queue.GetAdjustedMaxBatchSizeForTests(), 12);
	TestEqual(TEXT("Second 413 halves adjusted FlushEventCount with integer floor"), Queue.GetAdjustedFlushEventCountForTests(), 5);
	TestEqual(TEXT("Settings MaxBatchSize remains unchanged after second 413"), Settings->GetMaxBatchSize(), 50);
	TestEqual(TEXT("Settings FlushEventCount remains unchanged after second 413"), Settings->GetFlushEventCount(), 20);

	Clock.Advance(FTimespan::FromSeconds(10));
	TOptional<EPostHogEventQueueFlushResult> DrainResult;
	Queue.Flush([&DrainResult](EPostHogEventQueueFlushResult Result)
	{
		DrainResult = Result;
	});
	TestEqual(TEXT("Retry after second backoff sends the smaller batch"), Transport.GetTotalSendCount(), 3);
	TestEqual(TEXT("Second reduced retry batch uses adjusted MaxBatchSize"), Transport.GetPayloadAt(2).Num(), 12);
	CheckPayloadUuids(*this, Transport.GetPayloadAt(2), CopyEventIdRange(EventIds, 0, 12), TEXT("Second reduced retry batch"));

	int32 SuccessfulRetryBatches = 0;
	while (Transport.GetPendingCount() > 0)
	{
		Transport.CompleteLast(true, 200, TEXT(""));
		++SuccessfulRetryBatches;
	}

	TestEqual(TEXT("Adjusted retry drains in five successful batches"), SuccessfulRetryBatches, 5);
	TestEqual(TEXT("All retained events were deleted after successful drain"), Storage.DeleteAttempts, 60);
	TestEqual(TEXT("Queue drains retained events without recapture"), Queue.Num(), 0);
	TestTrue(TEXT("Successful drain completes the flush"), DrainResult.IsSet());
	if (DrainResult.IsSet())
	{
		TestEqual(TEXT("Successful drain result"), DrainResult.GetValue(), EPostHogEventQueueFlushResult::Drained);
	}
	TestEqual(TEXT("Adjusted MaxBatchSize is not restored during queue lifetime"), Queue.GetAdjustedMaxBatchSizeForTests(), 12);
	TestEqual(TEXT("Adjusted FlushEventCount is not restored during queue lifetime"), Queue.GetAdjustedFlushEventCountForTests(), 5);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueuePayloadTooLargeAdjustedThresholdTriggersLaterFlushTest, "UnrealHog.Events.EventQueue.PayloadTooLargeAdjustedThresholdTriggersLaterFlush", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueuePayloadTooLargeAdjustedThresholdTriggersLaterFlushTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("received HTTP 413"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("classified batch delivery failure as retryable; retaining attempted batch. StatusCode=413"), EAutomationExpectedErrorFlags::Contains, 1);

	FControllableQueueStorageProvider Storage;
	FPostHogFakeBatchTransport Transport;
	FPostHogFakeClock Clock;
	UPostHogDeveloperSettings* Settings = MakeTransientQueueSettings(50, 20);
	SeedNumberedEvents(Storage, 12);

	FPostHogEventQueue Queue(Storage, Transport, Settings->GetApiKey(), 100, Settings->GetMaxBatchSize(), Settings->GetFlushEventCount(), &Clock);
	Queue.Flush();
	TestEqual(TEXT("Initial threshold test sends one manual batch"), Transport.GetTotalSendCount(), 1);

	Transport.CompleteLast(false, 413, TEXT(""));
	TestEqual(TEXT("413 halves threshold to ten"), Queue.GetAdjustedFlushEventCountForTests(), 10);
	TestEqual(TEXT("413 halves MaxBatchSize to twenty-five"), Queue.GetAdjustedMaxBatchSizeForTests(), 25);

	Clock.Advance(FTimespan::FromSeconds(5));
	TOptional<EPostHogEventQueueFlushResult> DrainResult;
	Queue.Flush([&DrainResult](EPostHogEventQueueFlushResult Result)
	{
		DrainResult = Result;
	});
	TestEqual(TEXT("Retry after 413 sends retained events"), Transport.GetTotalSendCount(), 2);
	Transport.CompleteLast(true, 200, TEXT(""));
	TestTrue(TEXT("Retained events drain successfully"), DrainResult.IsSet());
	if (DrainResult.IsSet())
	{
		TestEqual(TEXT("Retained drain result"), DrainResult.GetValue(), EPostHogEventQueueFlushResult::Drained);
	}
	TestEqual(TEXT("Queue empty before adjusted-threshold enqueue"), Queue.Num(), 0);

	const int32 SendsBeforeThresholdProbe = Transport.GetTotalSendCount();
	for (int32 Index = 1; Index <= 9; ++Index)
	{
		const FString Suffix = FString::Printf(TEXT("adjusted-threshold-%d"), Index);
		TestEqual(*FString::Printf(TEXT("Enqueue %d below adjusted threshold succeeds"), Index), Queue.Enqueue(MakeTestEvent(Suffix)), EPostHogEventQueueEnqueueResult::Enqueued);
		TestEqual(*FString::Printf(TEXT("Enqueue %d below adjusted threshold does not send"), Index), Transport.GetTotalSendCount(), SendsBeforeThresholdProbe);
	}

	TestEqual(TEXT("Nine queued events stay below adjusted threshold"), Queue.Num(), 9);
	TestEqual(TEXT("Tenth adjusted-threshold enqueue succeeds"), Queue.Enqueue(MakeTestEvent(TEXT("adjusted-threshold-10"))), EPostHogEventQueueEnqueueResult::Enqueued);
	TestEqual(TEXT("Adjusted threshold triggers one later auto-flush"), Transport.GetTotalSendCount(), SendsBeforeThresholdProbe + 1);
	TestEqual(TEXT("Adjusted threshold payload contains ten events"), Transport.GetLastPayload().Num(), 10);
	TestEqual(TEXT("Settings FlushEventCount remains unchanged after threshold adjustment"), Settings->GetFlushEventCount(), 20);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueuePayloadTooLargeAtOneRetainsAndBacksOffTest, "UnrealHog.Events.EventQueue.PayloadTooLargeAtOneRetainsAndBacksOff", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueuePayloadTooLargeAtOneRetainsAndBacksOffTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("received HTTP 413"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("classified batch delivery failure as retryable; retaining attempted batch. StatusCode=413"), EAutomationExpectedErrorFlags::Contains, 1);

	FControllableQueueStorageProvider Storage;
	FPostHogFakeBatchTransport Transport;
	FPostHogFakeClock Clock;
	UPostHogDeveloperSettings* Settings = MakeTransientQueueSettings(1, 1);

	FPostHogEventQueue Queue(Storage, Transport, Settings->GetApiKey(), 100, Settings->GetMaxBatchSize(), Settings->GetFlushEventCount(), &Clock);
	const FPostHogEvent Event = MakeTestEvent(TEXT("payload-too-large-floor"));
	const FString EventId = Event.GetEventId();

	TestEqual(TEXT("Floor test enqueue succeeds"), Queue.Enqueue(Event), EPostHogEventQueueEnqueueResult::Enqueued);
	TestEqual(TEXT("Flush threshold one sends immediately"), Transport.GetTotalSendCount(), 1);
	TestEqual(TEXT("Floor test first payload has one event"), Transport.GetPayloadAt(0).Num(), 1);
	CheckPayloadUuids(*this, Transport.GetPayloadAt(0), { EventId }, TEXT("Floor initial batch"));

	Transport.CompleteLast(false, 413, TEXT(""));
	TestEqual(TEXT("413 at batch size one retains the event"), Queue.Num(), 1);
	TestEqual(TEXT("413 at batch size one deletes nothing"), Storage.DeleteAttempts, 0);
	TestEqual(TEXT("Adjusted MaxBatchSize remains at floor"), Queue.GetAdjustedMaxBatchSizeForTests(), 1);
	TestEqual(TEXT("Adjusted FlushEventCount remains at floor"), Queue.GetAdjustedFlushEventCountForTests(), 1);

	const EPostHogEventQueueFlushResult ImmediateRetryResult = FlushQueueAndCaptureResult(Queue);
	TestEqual(TEXT("Immediate retry at floor is paused"), ImmediateRetryResult, EPostHogEventQueueFlushResult::Paused);
	TestEqual(TEXT("Immediate retry at floor sends no request"), Transport.GetTotalSendCount(), 1);

	Clock.Advance(FTimespan::FromSeconds(5));
	TOptional<EPostHogEventQueueFlushResult> RetryResult;
	Queue.Flush([&RetryResult](EPostHogEventQueueFlushResult Result)
	{
		RetryResult = Result;
	});
	TestEqual(TEXT("Retry after floor backoff sends one event"), Transport.GetTotalSendCount(), 2);
	TestEqual(TEXT("Retry floor payload has one event"), Transport.GetPayloadAt(1).Num(), 1);
	CheckPayloadUuids(*this, Transport.GetPayloadAt(1), { EventId }, TEXT("Floor retry batch"));

	Transport.CompleteLast(true, 200, TEXT(""));
	TestEqual(TEXT("Floor retry success drains the retained event"), Queue.Num(), 0);
	TestTrue(TEXT("Floor retry result completes"), RetryResult.IsSet());
	if (RetryResult.IsSet())
	{
		TestEqual(TEXT("Floor retry result"), RetryResult.GetValue(), EPostHogEventQueueFlushResult::Drained);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueueEnqueueDuringFlushDrainedByActiveOperationTest, "UnrealHog.Events.EventQueue.EnqueueDuringFlushDrainedByActiveOperation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueueEnqueueDuringFlushDrainedByActiveOperationTest::RunTest(const FString& Parameters)
{
	FControllableQueueStorageProvider Storage;
	FPostHogFakeBatchTransport Transport;
	TOptional<EPostHogEventQueueFlushResult> Result;

	Storage.SeedEvent(SeedEventId1);
	Storage.SeedEvent(SeedEventId2);

	FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 100, 2, 100);
	Queue.Flush([&Result](EPostHogEventQueueFlushResult InResult)
	{
		Result = InResult;
	});

	const FPostHogEvent EnqueuedEvent = MakeTestEvent(TEXT("during-flush"));
	const FString EnqueuedEventId = EnqueuedEvent.GetEventId();
	TestEqual(TEXT("Enqueue during active flush succeeds"), Queue.Enqueue(EnqueuedEvent), EPostHogEventQueueEnqueueResult::Enqueued);
	TestEqual(TEXT("Enqueue during active flush does not overlap request"), Transport.GetPendingCount(), 1);
	TestEqual(TEXT("Enqueue during active flush creates no extra send yet"), Transport.GetTotalSendCount(), 1);

	Transport.CompleteLast(true, 200, TEXT(""));
	TestEqual(TEXT("Active flush picks up enqueued event"), Transport.GetPendingCount(), 1);
	TestEqual(TEXT("Enqueued event sent as next batch"), Transport.GetTotalSendCount(), 2);
	CheckPayloadUuids(*this, Transport.GetPayloadAt(1), { EnqueuedEventId }, TEXT("Enqueued during flush batch"));

	Transport.CompleteLast(true, 200, TEXT(""));
	TestEqual(TEXT("Queue drained including enqueued event"), Queue.Num(), 0);
	TestEqual(TEXT("No overlapping requests after enqueue drain"), Transport.GetPendingCount(), 0);
	TestTrue(TEXT("Flush completed"), Result.IsSet());
	if (Result.IsSet())
	{
		TestEqual(TEXT("Flush result"), Result.GetValue(), EPostHogEventQueueFlushResult::Drained);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueueCancellationCompletesFlushAndPreventsNextBatchTest, "UnrealHog.Events.EventQueue.CancellationCompletesFlushAndPreventsNextBatch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueueCancellationCompletesFlushAndPreventsNextBatchTest::RunTest(const FString& Parameters)
{
	FControllableQueueStorageProvider Storage;
	FPostHogFakeBatchTransport Transport;
	TOptional<EPostHogEventQueueFlushResult> Result;

	Storage.SeedEvent(SeedEventId1);
	Storage.SeedEvent(SeedEventId2);
	Storage.SeedEvent(SeedEventId3);

	FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 100, 2, 100);
	Queue.Flush([&Result](EPostHogEventQueueFlushResult InResult)
	{
		Result = InResult;
	});

	Queue.CancelInFlightRequest();
	TestTrue(TEXT("Request cancelled"), Transport.IsLastRequestCancelled());
	TestTrue(TEXT("Cancellation completes flush"), Result.IsSet());
	if (Result.IsSet())
	{
		TestEqual(TEXT("Cancellation result"), Result.GetValue(), EPostHogEventQueueFlushResult::Cancelled);
	}

	Transport.CompleteLast(true, 200, TEXT(""));
	TestEqual(TEXT("Late completion after cancellation sends no next batch"), Transport.GetTotalSendCount(), 1);
	TestEqual(TEXT("No pending request after cancelled late completion"), Transport.GetPendingCount(), 0);
	TestEqual(TEXT("Cancellation deletes no records"), Storage.DeleteAttempts, 0);
	CheckEventIds(*this, Storage, { SeedEventId1, SeedEventId2, SeedEventId3 }, TEXT("Cancellation final storage"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueueDeleteFailureStopsDrainAndPreservesFailedAndLaterRecordsTest, "UnrealHog.Events.EventQueue.DeleteFailureStopsDrainAndPreservesFailedAndLaterRecords", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueueDeleteFailureStopsDrainAndPreservesFailedAndLaterRecordsTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("deleting a sent event failed"), EAutomationExpectedErrorFlags::Contains, 1);

	FControllableQueueStorageProvider Storage;
	FPostHogFakeBatchTransport Transport;
	TOptional<EPostHogEventQueueFlushResult> Result;

	Storage.SeedEvent(SeedEventId1);
	Storage.SeedEvent(SeedEventId2);
	Storage.SeedEvent(SeedEventId3);
	Storage.SetFailNextDelete(true);

	FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 100, 2, 100);
	Queue.Flush([&Result](EPostHogEventQueueFlushResult InResult)
	{
		Result = InResult;
	});

	Transport.CompleteLast(true, 200, TEXT(""));

	TestEqual(TEXT("Delete failure sends no later batch"), Transport.GetTotalSendCount(), 1);
	TestEqual(TEXT("No pending request after delete failure"), Transport.GetPendingCount(), 0);
	TestEqual(TEXT("Delete failure attempted once"), Storage.DeleteAttempts, 1);
	TestEqual(TEXT("Failed delete targeted first sent id"), Storage.LastDeletedEventId, SeedEventId1);
	CheckEventIds(*this, Storage, { SeedEventId1, SeedEventId2, SeedEventId3 }, TEXT("Delete failure final storage"));
	TestTrue(TEXT("Delete failure completes flush"), Result.IsSet());
	if (Result.IsSet())
	{
		TestEqual(TEXT("Delete failure result"), Result.GetValue(), EPostHogEventQueueFlushResult::ProgressBlocked);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueueCorruptFirstRecordDeletedAndLaterRecordsFillBatchTest, "UnrealHog.Events.EventQueue.CorruptFirstRecordDeletedAndLaterRecordsFillBatch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueueCorruptFirstRecordDeletedAndLaterRecordsFillBatchTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("deleted corrupt persisted event"), EAutomationExpectedErrorFlags::Contains, 1);

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
	AddExpectedError(TEXT("deleted corrupt persisted event"), EAutomationExpectedErrorFlags::Contains, 1);

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
	AddExpectedError(TEXT("failed to delete corrupt persisted event"), EAutomationExpectedErrorFlags::Contains, 1);

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
	AddExpectedError(TEXT("classified batch delivery failure as retryable; retaining attempted batch. StatusCode=500"), EAutomationExpectedErrorFlags::Contains, 1);

	FScopedQueueTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Storage(Fixture.GetRootPath());
	FPostHogFakeBatchTransport Transport;
	FPostHogFakeClock Clock;

	FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 100, 100, 2, &Clock);

	Queue.Enqueue(MakeTestEvent(TEXT("1")));
	Queue.Enqueue(MakeTestEvent(TEXT("2")));
	TestEqual(TEXT("Flush occurred"), Transport.GetSentCount(), 1);

	Transport.CompleteLast(false, 500, TEXT(""));

	TestEqual(TEXT("Failed flush leaves events queued"), Queue.Num(), 2);
	TestEqual(TEXT("Storage retains the un-acknowledged events"), Storage.GetEventCount(), 2);

	// A retryable failure enters backoff; an immediate retry must not create a new HTTP request.
	EPostHogEventQueueFlushResult ImmediateRetryResult = EPostHogEventQueueFlushResult::Empty;
	Queue.Flush([&ImmediateRetryResult](EPostHogEventQueueFlushResult Result) { ImmediateRetryResult = Result; });
	TestEqual(TEXT("Immediate retry during backoff is paused"), static_cast<uint8>(ImmediateRetryResult), static_cast<uint8>(EPostHogEventQueueFlushResult::Paused));
	TestEqual(TEXT("Paused retry does not issue a new send"), Transport.GetSentCount(), 0);

	// bIsFlushing must have reset so a later Flush() can retry rather than silently no-op.
	Clock.Advance(FTimespan::FromSeconds(5));
	Queue.Flush();
	TestEqual(TEXT("Retry after backoff elapses issues a new send"), Transport.GetSentCount(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventQueueSynchronousFailureCompletesOnceTest, "UnrealHog.Events.EventQueue.SynchronousSendStartFailureCompletesOnceAndAllowsRetry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventQueueSynchronousFailureCompletesOnceTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("classified batch delivery failure as retryable; retaining attempted batch. StatusCode=0"), EAutomationExpectedErrorFlags::Contains, 1);

	FScopedQueueTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Storage(Fixture.GetRootPath());
	FPostHogFakeBatchTransport Transport;
	FPostHogFakeClock Clock;

	FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 100, 100, 2, &Clock);

	Transport.SetSynchronousFailure(true);

	Queue.Enqueue(MakeTestEvent(TEXT("1")));
	Queue.Enqueue(MakeTestEvent(TEXT("2")));

	TestEqual(TEXT("Synchronous failure does not register a pending send"), Transport.GetSentCount(), 0);
	TestEqual(TEXT("Events remain queued after a synchronous send-start failure"), Queue.Num(), 2);

	// StatusCode 0 is retryable, so the synchronous failure also enters backoff; an immediate
	// retry must not create a new HTTP request even once the transport is healthy again.
	Transport.SetSynchronousFailure(false);
	EPostHogEventQueueFlushResult ImmediateRetryResult = EPostHogEventQueueFlushResult::Empty;
	Queue.Flush([&ImmediateRetryResult](EPostHogEventQueueFlushResult Result) { ImmediateRetryResult = Result; });
	TestEqual(TEXT("Immediate retry during backoff is paused"), static_cast<uint8>(ImmediateRetryResult), static_cast<uint8>(EPostHogEventQueueFlushResult::Paused));
	TestEqual(TEXT("Paused retry does not issue a new send"), Transport.GetSentCount(), 0);

	// A stuck bIsFlushing would make this Flush() a silent no-op; it must actually retry.
	Clock.Advance(FTimespan::FromSeconds(5));
	Queue.Flush();
	TestEqual(TEXT("Queue is flushable again after backoff elapses"), Transport.GetSentCount(), 1);

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
	AddExpectedError(TEXT("every persisted record is in flight"), EAutomationExpectedErrorFlags::Contains, 1);

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
	AddExpectedError(TEXT("capacity eviction failed"), EAutomationExpectedErrorFlags::Contains, 1);

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
	AddExpectedError(TEXT("SaveEvent failed"), EAutomationExpectedErrorFlags::Contains, 1);

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
