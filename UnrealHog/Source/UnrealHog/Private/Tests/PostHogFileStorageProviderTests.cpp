// Trevor Eckhoff, 2026. All rights reserved.

#include "Storage/PostHogFileStorageProvider.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "SDK/PostHogSdkInfo.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Tasks/Task.h"

namespace
{
	// RAII fixture that owns a unique temporary directory and guarantees its removal,
	// including on assertion failure, since the destructor always runs when the scope exits.
	class FScopedTestStorageDirectory
	{
	public:
		FScopedTestStorageDirectory()
			: RootPath(FPaths::Combine(FPlatformProcess::UserTempDir(), TEXT("UnrealHogTests"), FGuid::NewGuid().ToString(EGuidFormats::Digits)))
		{
		}

		~FScopedTestStorageDirectory()
		{
			IFileManager::Get().DeleteDirectory(*RootPath, false, true);
		}

		const FString& GetRootPath() const { return RootPath; }

		FString GetQueueDirectory() const
		{
			return FPaths::Combine(RootPath, PostHogSdkInfo::GetLibraryName(), TEXT("Queue"));
		}

		FString GetStateDirectory() const
		{
			return FPaths::Combine(RootPath, PostHogSdkInfo::GetLibraryName(), TEXT("State"));
		}

	private:
		FString RootPath;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFileStorageCreatesDirectoriesTest, "UnrealHog.Storage.FileStorageProvider.CreatesQueueAndStateDirectories", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFileStorageCreatesDirectoriesTest::RunTest(const FString& Parameters)
{
	FScopedTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Provider(Fixture.GetRootPath());

	TestTrue(TEXT("Queue directory exists"), IFileManager::Get().DirectoryExists(*Fixture.GetQueueDirectory()));
	TestTrue(TEXT("State directory exists"), IFileManager::Get().DirectoryExists(*Fixture.GetStateDirectory()));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFileStorageLoadsExistingEventsTest, "UnrealHog.Storage.FileStorageProvider.LoadsExistingEventsFromDisk", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFileStorageLoadsExistingEventsTest::RunTest(const FString& Parameters)
{
	FScopedTestStorageDirectory Fixture;

	// Seed files before construction so InitializeDirectories() creates the tree first.
	{
		FPostHogFileStorageProvider SeedProvider(Fixture.GetRootPath());
	}

	FFileHelper::SaveStringToFile(TEXT("{\"a\":1}"), *FPaths::Combine(Fixture.GetQueueDirectory(), TEXT("bbb.json")));
	FFileHelper::SaveStringToFile(TEXT("{\"a\":2}"), *FPaths::Combine(Fixture.GetQueueDirectory(), TEXT("aaa.json")));

	FPostHogFileStorageProvider Provider(Fixture.GetRootPath());
	const TArray<FString> EventIds = Provider.GetEventIds();

	TestEqual(TEXT("Loads both seeded events"), EventIds.Num(), 2);
	if (EventIds.Num() == 2)
	{
		TestEqual(TEXT("Lexically sorted, first"), EventIds[0], TEXT("aaa"));
		TestEqual(TEXT("Lexically sorted, second"), EventIds[1], TEXT("bbb"));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFileStorageSaveEventVisibleImmediatelyTest, "UnrealHog.Storage.FileStorageProvider.SaveEventVisibleImmediately", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFileStorageSaveEventVisibleImmediatelyTest::RunTest(const FString& Parameters)
{
	FScopedTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Provider(Fixture.GetRootPath());

	TestTrue(TEXT("SaveEvent succeeds"), Provider.SaveEvent(TEXT("event-1"), TEXT("{\"uuid\":\"event-1\"}")));
	TestTrue(TEXT("GetEventIds contains the new event"), Provider.GetEventIds().Contains(TEXT("event-1")));
	TestEqual(TEXT("GetEventCount reflects the new event"), Provider.GetEventCount(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFileStorageEventRoundTripTest, "UnrealHog.Storage.FileStorageProvider.SaveAndLoadEventRoundTripsExactJson", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFileStorageEventRoundTripTest::RunTest(const FString& Parameters)
{
	FScopedTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Provider(Fixture.GetRootPath());

	const FString OriginalJson = TEXT("{\"event\":\"test\",\"value\":42}");
	TestTrue(TEXT("SaveEvent succeeds"), Provider.SaveEvent(TEXT("event-1"), OriginalJson));

	FString LoadedJson;
	TestTrue(TEXT("LoadEvent succeeds"), Provider.LoadEvent(TEXT("event-1"), LoadedJson));
	TestEqual(TEXT("Loaded JSON matches saved JSON exactly"), LoadedJson, OriginalJson);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFileStorageMultipleEventsTest, "UnrealHog.Storage.FileStorageProvider.MultipleEventsAllWrittenToDisk", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFileStorageMultipleEventsTest::RunTest(const FString& Parameters)
{
	FScopedTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Provider(Fixture.GetRootPath());

	for (int32 Index = 0; Index < 5; ++Index)
	{
		const FString EventId = FString::Printf(TEXT("event-%d"), Index);
		const FString Json = FString::Printf(TEXT("{\"index\":%d}"), Index);
		TestTrue(*FString::Printf(TEXT("SaveEvent %d succeeds"), Index), Provider.SaveEvent(EventId, Json));
	}

	TestEqual(TEXT("All 5 events counted"), Provider.GetEventCount(), 5);

	for (int32 Index = 0; Index < 5; ++Index)
	{
		const FString EventId = FString::Printf(TEXT("event-%d"), Index);
		const FString Expected = FString::Printf(TEXT("{\"index\":%d}"), Index);

		FString Loaded;
		TestTrue(*FString::Printf(TEXT("LoadEvent %d succeeds"), Index), Provider.LoadEvent(EventId, Loaded));
		TestEqual(*FString::Printf(TEXT("Event %d content matches"), Index), Loaded, Expected);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFileStorageDuplicateEventIdTest, "UnrealHog.Storage.FileStorageProvider.DuplicateEventIdDoesNotDuplicateInIndex", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFileStorageDuplicateEventIdTest::RunTest(const FString& Parameters)
{
	FScopedTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Provider(Fixture.GetRootPath());

	TestTrue(TEXT("First save succeeds"), Provider.SaveEvent(TEXT("event-1"), TEXT("{\"v\":1}")));
	TestTrue(TEXT("Second save (overwrite) succeeds"), Provider.SaveEvent(TEXT("event-1"), TEXT("{\"v\":2}")));

	TestEqual(TEXT("Only one entry in the index"), Provider.GetEventCount(), 1);

	FString Loaded;
	TestTrue(TEXT("LoadEvent succeeds"), Provider.LoadEvent(TEXT("event-1"), Loaded));
	TestEqual(TEXT("Latest write wins"), Loaded, TEXT("{\"v\":2}"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFileStorageNonExistentEventTest, "UnrealHog.Storage.FileStorageProvider.NonExistentEventReturnsFalse", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFileStorageNonExistentEventTest::RunTest(const FString& Parameters)
{
	FScopedTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Provider(Fixture.GetRootPath());

	FString LoadedJson = TEXT("sentinel");
	TestFalse(TEXT("LoadEvent returns false"), Provider.LoadEvent(TEXT("missing-event"), LoadedJson));
	TestTrue(TEXT("Out param cleared"), LoadedJson.IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFileStorageDeleteEventTest, "UnrealHog.Storage.FileStorageProvider.DeleteEventRemovesFromIndexAndDisk", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFileStorageDeleteEventTest::RunTest(const FString& Parameters)
{
	FScopedTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Provider(Fixture.GetRootPath());

	Provider.SaveEvent(TEXT("event-1"), TEXT("{}"));
	TestTrue(TEXT("Delete succeeds"), Provider.DeleteEvent(TEXT("event-1")));

	TestFalse(TEXT("No longer in index"), Provider.GetEventIds().Contains(TEXT("event-1")));
	TestEqual(TEXT("Count reflects deletion"), Provider.GetEventCount(), 0);

	FString Loaded;
	TestFalse(TEXT("File removed from disk"), Provider.LoadEvent(TEXT("event-1"), Loaded));

	// Repeated deletion of an already-absent file remains a safe, idempotent success.
	TestTrue(TEXT("Repeated delete stays a safe success"), Provider.DeleteEvent(TEXT("event-1")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFileStorageClearEventsTest, "UnrealHog.Storage.FileStorageProvider.ClearEventsRemovesAllButKeepsDirectoryUsable", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFileStorageClearEventsTest::RunTest(const FString& Parameters)
{
	FScopedTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Provider(Fixture.GetRootPath());

	Provider.SaveEvent(TEXT("event-1"), TEXT("{}"));
	Provider.SaveEvent(TEXT("event-2"), TEXT("{}"));
	Provider.SaveEvent(TEXT("event-3"), TEXT("{}"));

	TestTrue(TEXT("ClearEvents succeeds"), Provider.ClearEvents());
	TestEqual(TEXT("No events remain"), Provider.GetEventCount(), 0);

	// The queue directory must remain usable for subsequent saves.
	TestTrue(TEXT("Directory still usable after clear"), Provider.SaveEvent(TEXT("event-4"), TEXT("{}")));
	TestEqual(TEXT("New event visible after clear"), Provider.GetEventCount(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFileStorageReturnsAllEventIdsTest, "UnrealHog.Storage.FileStorageProvider.ReturnsAllAndOnlySavedEventIds", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFileStorageReturnsAllEventIdsTest::RunTest(const FString& Parameters)
{
	FScopedTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Provider(Fixture.GetRootPath());

	const TArray<FString> Expected = { TEXT("alpha"), TEXT("beta"), TEXT("gamma") };
	for (const FString& EventId : Expected)
	{
		Provider.SaveEvent(EventId, TEXT("{}"));
	}

	const TArray<FString> Actual = Provider.GetEventIds();
	TestEqual(TEXT("Count matches"), Actual.Num(), Expected.Num());
	for (const FString& EventId : Expected)
	{
		TestTrue(*FString::Printf(TEXT("Contains %s"), *EventId), Actual.Contains(EventId));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFileStorageStateRoundTripTest, "UnrealHog.Storage.FileStorageProvider.SaveLoadDeleteStateRoundTrips", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFileStorageStateRoundTripTest::RunTest(const FString& Parameters)
{
	FScopedTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Provider(Fixture.GetRootPath());

	const FString StateJson = TEXT("{\"session_id\":\"abc123\"}");
	TestTrue(TEXT("SaveState succeeds"), Provider.SaveState(TEXT("session"), StateJson));

	FString LoadedJson;
	TestTrue(TEXT("LoadState succeeds"), Provider.LoadState(TEXT("session"), LoadedJson));
	TestEqual(TEXT("Loaded state content matches"), LoadedJson, StateJson);

	TestTrue(TEXT("DeleteState succeeds"), Provider.DeleteState(TEXT("session")));

	FString AfterDelete = TEXT("sentinel");
	TestFalse(TEXT("LoadState fails after delete"), Provider.LoadState(TEXT("session"), AfterDelete));
	TestTrue(TEXT("Out param cleared after failed load"), AfterDelete.IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFileStorageLoadStateNonExistentTest, "UnrealHog.Storage.FileStorageProvider.LoadStateNonExistentReturnsFalse", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFileStorageLoadStateNonExistentTest::RunTest(const FString& Parameters)
{
	FScopedTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Provider(Fixture.GetRootPath());

	FString LoadedJson = TEXT("sentinel");
	TestFalse(TEXT("LoadState returns false"), Provider.LoadState(TEXT("missing-state"), LoadedJson));
	TestTrue(TEXT("Out param cleared"), LoadedJson.IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFileStorageEmptyKeysFailTest, "UnrealHog.Storage.FileStorageProvider.EmptyEventIdOrStateKeyFailsAllOperations", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFileStorageEmptyKeysFailTest::RunTest(const FString& Parameters)
{
	FScopedTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Provider(Fixture.GetRootPath());

	FString Unused;
	TestFalse(TEXT("SaveEvent with empty ID fails"), Provider.SaveEvent(TEXT(""), TEXT("{}")));
	TestFalse(TEXT("LoadEvent with empty ID fails"), Provider.LoadEvent(TEXT(""), Unused));
	TestFalse(TEXT("DeleteEvent with empty ID fails"), Provider.DeleteEvent(TEXT("")));

	TestFalse(TEXT("SaveState with empty key fails"), Provider.SaveState(TEXT(""), TEXT("{}")));
	TestFalse(TEXT("LoadState with empty key fails"), Provider.LoadState(TEXT(""), Unused));
	TestFalse(TEXT("DeleteState with empty key fails"), Provider.DeleteState(TEXT("")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFileStorageJsonObjectOverloadsTest, "UnrealHog.Storage.FileStorageProvider.JsonObjectOverloadsSerializeValidObjects", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFileStorageJsonObjectOverloadsTest::RunTest(const FString& Parameters)
{
	FScopedTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Provider(Fixture.GetRootPath());

	const TSharedRef<FJsonObject> EventObject = MakeShared<FJsonObject>();
	EventObject->SetStringField(TEXT("event"), TEXT("test"));
	EventObject->SetNumberField(TEXT("value"), 7);

	TestTrue(TEXT("SaveEvent(JsonObject) succeeds"), Provider.SaveEvent(TEXT("event-json"), EventObject));

	FString LoadedJson;
	TestTrue(TEXT("LoadEvent succeeds"), Provider.LoadEvent(TEXT("event-json"), LoadedJson));

	TSharedPtr<FJsonObject> ParsedObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(LoadedJson);
	TestTrue(TEXT("Stored JSON is parseable"), FJsonSerializer::Deserialize(Reader, ParsedObject));

	FString EventField;
	TestTrue(TEXT("event field preserved as string"), ParsedObject->TryGetStringField(TEXT("event"), EventField));
	TestEqual(TEXT("event field value"), EventField, TEXT("test"));

	double ValueField = 0.0;
	TestTrue(TEXT("value field preserved as number"), ParsedObject->TryGetNumberField(TEXT("value"), ValueField));
	TestEqual(TEXT("value field value"), ValueField, 7.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFileStorageWaitsBeforeReadingTest, "UnrealHog.Storage.FileStorageProvider.WaitsForPendingWriteBeforeReading", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFileStorageWaitsBeforeReadingTest::RunTest(const FString& Parameters)
{
	FScopedTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Provider(Fixture.GetRootPath());

	const FString OriginalJson = TEXT("{\"event\":\"waits-before-reading\"}");
	TestTrue(TEXT("SaveEvent succeeds"), Provider.SaveEvent(TEXT("event-1"), OriginalJson));

	// No explicit flush: LoadEvent must itself wait for the queued write to land on disk.
	FString LoadedJson;
	TestTrue(TEXT("LoadEvent succeeds without an explicit flush"), Provider.LoadEvent(TEXT("event-1"), LoadedJson));
	TestEqual(TEXT("Loaded JSON matches saved JSON exactly"), LoadedJson, OriginalJson);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFileStorageWaitsBeforeDeletingTest, "UnrealHog.Storage.FileStorageProvider.WaitsForPendingWriteBeforeDeleting", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFileStorageWaitsBeforeDeletingTest::RunTest(const FString& Parameters)
{
	FScopedTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Provider(Fixture.GetRootPath());

	TestTrue(TEXT("SaveEvent succeeds"), Provider.SaveEvent(TEXT("event-1"), TEXT("{}")));

	// No explicit flush: DeleteEvent must itself wait for the queued write before removing the file.
	TestTrue(TEXT("DeleteEvent succeeds without an explicit flush"), Provider.DeleteEvent(TEXT("event-1")));
	TestFalse(TEXT("Event file absent from disk"), FPaths::FileExists(FPaths::Combine(Fixture.GetQueueDirectory(), TEXT("event-1.json"))));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFileStorageFlushBlocksUntilCompleteTest, "UnrealHog.Storage.FileStorageProvider.FlushPendingWritesBlocksUntilAllWritesComplete", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFileStorageFlushBlocksUntilCompleteTest::RunTest(const FString& Parameters)
{
	FScopedTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Provider(Fixture.GetRootPath());

	constexpr int32 EventCount = 10;
	for (int32 Index = 0; Index < EventCount; ++Index)
	{
		Provider.SaveEvent(FString::Printf(TEXT("event-%d"), Index), FString::Printf(TEXT("{\"index\":%d}"), Index));
	}

	Provider.FlushPendingWrites();

	for (int32 Index = 0; Index < EventCount; ++Index)
	{
		const FString EventFilePath = FPaths::Combine(Fixture.GetQueueDirectory(), FString::Printf(TEXT("event-%d.json"), Index));
		TestTrue(*FString::Printf(TEXT("Event %d file exists after flush"), Index), FPaths::FileExists(EventFilePath));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFileStorageFlushWithNoPendingWritesTest, "UnrealHog.Storage.FileStorageProvider.FlushPendingWritesWithNoPendingWritesReturnsImmediately", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFileStorageFlushWithNoPendingWritesTest::RunTest(const FString& Parameters)
{
	FScopedTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Provider(Fixture.GetRootPath());

	// A fresh provider has no pending writes; flushing must not hang.
	Provider.FlushPendingWrites();

	TestEqual(TEXT("No events present"), Provider.GetEventCount(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFileStorageFlushIsIdempotentTest, "UnrealHog.Storage.FileStorageProvider.FlushPendingWritesCalledMultipleTimesIsIdempotent", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFileStorageFlushIsIdempotentTest::RunTest(const FString& Parameters)
{
	FScopedTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Provider(Fixture.GetRootPath());

	Provider.SaveEvent(TEXT("event-1"), TEXT("{}"));

	Provider.FlushPendingWrites();
	Provider.FlushPendingWrites();
	Provider.FlushPendingWrites();

	TestTrue(TEXT("Event file exists after repeated flushes"), FPaths::FileExists(FPaths::Combine(Fixture.GetQueueDirectory(), TEXT("event-1.json"))));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFileStorageClearWaitsBeforeClearingTest, "UnrealHog.Storage.FileStorageProvider.ClearEventsWaitsForPendingWritesBeforeClearing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFileStorageClearWaitsBeforeClearingTest::RunTest(const FString& Parameters)
{
	FScopedTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Provider(Fixture.GetRootPath());

	Provider.SaveEvent(TEXT("event-1"), TEXT("{}"));

	// No explicit flush: ClearEvents must itself wait for the queued write before clearing.
	TestTrue(TEXT("ClearEvents succeeds without an explicit flush"), Provider.ClearEvents());

	TestEqual(TEXT("No events remain in the index"), Provider.GetEventIds().Num(), 0);
	TestFalse(TEXT("Event file absent from disk"), FPaths::FileExists(FPaths::Combine(Fixture.GetQueueDirectory(), TEXT("event-1.json"))));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFileStorageConcurrentSavesTest, "UnrealHog.Storage.FileStorageProvider.ConcurrentSavesDoNotCorruptIndex", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFileStorageConcurrentSavesTest::RunTest(const FString& Parameters)
{
	FScopedTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Provider(Fixture.GetRootPath());

	constexpr int32 EventCount = 100;
	TArray<UE::Tasks::FTask> Tasks;
	Tasks.Reserve(EventCount);

	for (int32 Index = 0; Index < EventCount; ++Index)
	{
		const FString EventId = FString::Printf(TEXT("event-%d"), Index);
		Tasks.Add(UE::Tasks::Launch(UE_SOURCE_LOCATION, [&Provider, EventId]()
		{
			Provider.SaveEvent(EventId, TEXT("{}"));
		}));
	}

	UE::Tasks::Wait(Tasks);
	Provider.FlushPendingWrites();

	const TArray<FString> EventIds = Provider.GetEventIds();
	TestEqual(TEXT("Event count matches concurrent save count"), Provider.GetEventCount(), EventCount);

	TSet<FString> UniqueIds(EventIds);
	TestEqual(TEXT("No duplicate or missing IDs in the index"), UniqueIds.Num(), EventCount);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFileStorageConcurrentSavesAndLoadsTest, "UnrealHog.Storage.FileStorageProvider.ConcurrentSavesAndLoadsDoNotCorrupt", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFileStorageConcurrentSavesAndLoadsTest::RunTest(const FString& Parameters)
{
	FScopedTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Provider(Fixture.GetRootPath());

	constexpr int32 EventCount = 50;
	TArray<UE::Tasks::TTask<bool>> Tasks;
	Tasks.Reserve(EventCount);

	for (int32 Index = 0; Index < EventCount; ++Index)
	{
		const FString EventId = FString::Printf(TEXT("event-%d"), Index);
		const FString Expected = FString::Printf(TEXT("{\"index\":%d}"), Index);

		Tasks.Add(UE::Tasks::Launch(UE_SOURCE_LOCATION, [&Provider, EventId, Expected]() -> bool
		{
			Provider.SaveEvent(EventId, Expected);

			FString Loaded;
			return Provider.LoadEvent(EventId, Loaded) && Loaded == Expected;
		}));
	}

	// Test* macros are not thread-safe: collect results here on the test thread only.
	bool bAllMatched = true;
	for (UE::Tasks::TTask<bool>& Task : Tasks)
	{
		bAllMatched &= Task.GetResult();
	}

	TestTrue(TEXT("Every concurrent save+load pair round-tripped its own data"), bAllMatched);
	TestEqual(TEXT("Event count matches concurrent save count"), Provider.GetEventCount(), EventCount);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFileStorageConcurrentSavesAndDeletesTest, "UnrealHog.Storage.FileStorageProvider.ConcurrentSavesAndDeletesDoNotThrow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFileStorageConcurrentSavesAndDeletesTest::RunTest(const FString& Parameters)
{
	FScopedTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Provider(Fixture.GetRootPath());

	constexpr int32 EventCount = 50;
	for (int32 Index = 0; Index < EventCount; ++Index)
	{
		Provider.SaveEvent(FString::Printf(TEXT("event-%d"), Index), TEXT("{}"));
	}
	Provider.FlushPendingWrites();

	TArray<UE::Tasks::FTask> Tasks;
	Tasks.Reserve(EventCount / 2);

	for (int32 Index = 0; Index < EventCount; Index += 2)
	{
		const FString EventId = FString::Printf(TEXT("event-%d"), Index);
		Tasks.Add(UE::Tasks::Launch(UE_SOURCE_LOCATION, [&Provider, EventId]()
		{
			Provider.DeleteEvent(EventId);
		}));
	}

	UE::Tasks::Wait(Tasks);
	Provider.FlushPendingWrites();

	TestTrue(TEXT("At least half the events remain"), Provider.GetEventCount() >= EventCount / 2);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
