// Trevor Eckhoff, 2026. All rights reserved.

#include "Storage/PostHogFileStorageProvider.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Async/ParallelFor.h"
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

#include <atomic>

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

		FString GetEventFilePath(const FString& EventId) const
		{
			return FPaths::Combine(GetQueueDirectory(), FString::Printf(TEXT("%s.json"), *EventId));
		}

		FString GetStateFilePath(const FString& StateKey) const
		{
			return FPaths::Combine(GetStateDirectory(), FString::Printf(TEXT("%s.json"), *StateKey));
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

	Provider.FlushPendingWrites();
	FString SavedJson;
	TestTrue(TEXT("Event file is written after flush"), FFileHelper::LoadFileToString(SavedJson, *Fixture.GetEventFilePath(TEXT("event-1"))));
	TestEqual(TEXT("Event file JSON is exact"), SavedJson, TEXT("{\"uuid\":\"event-1\"}"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFileStorageWritesEventToDiskAsynchronouslyTest, "UnrealHog.Storage.FileStorageProvider.WritesEventToDisk_Asynchronously", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFileStorageWritesEventToDiskAsynchronouslyTest::RunTest(const FString& Parameters)
{
	FScopedTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Provider(Fixture.GetRootPath());

	const FString OriginalJson = TEXT("{\"event\":\"async-write\",\"value\":42}");
	TestTrue(TEXT("SaveEvent accepts async write"), Provider.SaveEvent(TEXT("event-async"), OriginalJson));
	Provider.FlushPendingWrites();

	FString SavedJson;
	TestTrue(TEXT("Event file exists after flush"), FFileHelper::LoadFileToString(SavedJson, *Fixture.GetEventFilePath(TEXT("event-async"))));
	TestEqual(TEXT("Event file content matches"), SavedJson, OriginalJson);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFileStorageFailedEventWriteRemovesIndexAfterFlushTest, "UnrealHog.Storage.FileStorageProvider.FailedEventWrite_RemovesIndexAfterFlush", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFileStorageFailedEventWriteRemovesIndexAfterFlushTest::RunTest(const FString& Parameters)
{
	FScopedTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Provider(Fixture.GetRootPath());

	TestTrue(TEXT("Queue directory removed for failure setup"), IFileManager::Get().DeleteDirectory(*Fixture.GetQueueDirectory(), false, true));
	TestTrue(TEXT("Queue path replaced by file"), FFileHelper::SaveStringToFile(TEXT("blocked"), *Fixture.GetQueueDirectory()));

	TestTrue(TEXT("SaveEvent accepts async write before failure is known"), Provider.SaveEvent(TEXT("event-fail"), TEXT("{\"blocked\":true}")));
	TestTrue(TEXT("Pending failed event visible before flush"), Provider.GetEventIds().Contains(TEXT("event-fail")));

	Provider.FlushPendingWrites();

	TestFalse(TEXT("Failed write removed from index after flush"), Provider.GetEventIds().Contains(TEXT("event-fail")));
	TestEqual(TEXT("Failed write not counted after flush"), Provider.GetEventCount(), 0);

	FString LoadedJson;
	TestFalse(TEXT("Failed write cannot be loaded"), Provider.LoadEvent(TEXT("event-fail"), LoadedJson));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFileStorageFailedStateWriteObservableAfterFlushTest, "UnrealHog.Storage.FileStorageProvider.FailedStateWrite_ObservableAfterFlush", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFileStorageFailedStateWriteObservableAfterFlushTest::RunTest(const FString& Parameters)
{
	FScopedTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Provider(Fixture.GetRootPath());

	TestTrue(TEXT("State directory removed for failure setup"), IFileManager::Get().DeleteDirectory(*Fixture.GetStateDirectory(), false, true));
	TestTrue(TEXT("State path replaced by file"), FFileHelper::SaveStringToFile(TEXT("blocked"), *Fixture.GetStateDirectory()));

	TestTrue(TEXT("SaveState accepts async write before failure is known"), Provider.SaveState(TEXT("state-fail"), TEXT("{\"blocked\":true}")));
	Provider.FlushPendingWrites();

	FString LoadedJson;
	TestFalse(TEXT("Failed state write cannot be loaded after flush"), Provider.LoadState(TEXT("state-fail"), LoadedJson));

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFileStorageFailedEventDeleteRestoresIndexAfterFlushTest, "UnrealHog.Storage.FileStorageProvider.FailedEventDelete_RestoresIndexAfterFlush", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFileStorageFailedEventDeleteRestoresIndexAfterFlushTest::RunTest(const FString& Parameters)
{
	FScopedTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Provider(Fixture.GetRootPath());

	TestTrue(TEXT("SaveEvent succeeds"), Provider.SaveEvent(TEXT("event-delete-fail"), TEXT("{}")));
	Provider.FlushPendingWrites();

	const FString EventFilePath = Fixture.GetEventFilePath(TEXT("event-delete-fail"));
	TestTrue(TEXT("Event file removed for failure setup"), IFileManager::Get().Delete(*EventFilePath, false, true));
	TestTrue(TEXT("Event file path replaced by directory"), IFileManager::Get().MakeDirectory(*EventFilePath, true));

	TestTrue(TEXT("DeleteEvent accepts async delete before failure is known"), Provider.DeleteEvent(TEXT("event-delete-fail")));
	TestFalse(TEXT("Pending delete removes event before flush"), Provider.GetEventIds().Contains(TEXT("event-delete-fail")));

	Provider.FlushPendingWrites();

	TestTrue(TEXT("Failed delete restores event in index after flush"), Provider.GetEventIds().Contains(TEXT("event-delete-fail")));
	TestEqual(TEXT("Failed delete keeps event counted after flush"), Provider.GetEventCount(), 1);

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
	Provider.FlushPendingWrites();
	TestTrue(TEXT("New event file written after clear"), FPaths::FileExists(Fixture.GetEventFilePath(TEXT("event-4"))));

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

	Provider.FlushPendingWrites();
	FString SavedJson;
	TestTrue(TEXT("State file exists after flush"), FFileHelper::LoadFileToString(SavedJson, *Fixture.GetStateFilePath(TEXT("session"))));
	TestEqual(TEXT("State file content matches"), SavedJson, StateJson);

	TestTrue(TEXT("DeleteState succeeds"), Provider.DeleteState(TEXT("session")));
	Provider.FlushPendingWrites();
	TestFalse(TEXT("State file removed from disk"), FPaths::FileExists(Fixture.GetStateFilePath(TEXT("session"))));

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFileStorageWaitsForPendingWriteBeforeReadingTest, "UnrealHog.Storage.FileStorageProvider.WaitsForPendingWrite_BeforeReading", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFileStorageWaitsForPendingWriteBeforeReadingTest::RunTest(const FString& Parameters)
{
	FScopedTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Provider(Fixture.GetRootPath());

	const FString OriginalJson = TEXT("{\"pending\":\"read\"}");
	TestTrue(TEXT("SaveEvent accepts pending write"), Provider.SaveEvent(TEXT("event-1"), OriginalJson));

	FString LoadedJson;
	TestTrue(TEXT("LoadEvent waits for pending write"), Provider.LoadEvent(TEXT("event-1"), LoadedJson));
	TestEqual(TEXT("Loaded pending event matches"), LoadedJson, OriginalJson);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFileStorageWaitsForPendingWriteBeforeDeletingTest, "UnrealHog.Storage.FileStorageProvider.WaitsForPendingWrite_BeforeDeleting", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFileStorageWaitsForPendingWriteBeforeDeletingTest::RunTest(const FString& Parameters)
{
	FScopedTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Provider(Fixture.GetRootPath());

	TestTrue(TEXT("SaveEvent accepts pending write"), Provider.SaveEvent(TEXT("event-1"), TEXT("{\"pending\":\"delete\"}")));
	TestTrue(TEXT("DeleteEvent accepts delete after pending write"), Provider.DeleteEvent(TEXT("event-1")));
	Provider.FlushPendingWrites();

	TestFalse(TEXT("Deleted event file absent"), FPaths::FileExists(Fixture.GetEventFilePath(TEXT("event-1"))));
	TestEqual(TEXT("Deleted event removed from index"), Provider.GetEventCount(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFileStorageFlushBlocksUntilAllWritesCompleteTest, "UnrealHog.Storage.FileStorageProvider.FlushPendingWrites.BlocksUntilAllWritesComplete", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFileStorageFlushBlocksUntilAllWritesCompleteTest::RunTest(const FString& Parameters)
{
	FScopedTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Provider(Fixture.GetRootPath());

	for (int32 Index = 0; Index < 10; ++Index)
	{
		const FString EventId = FString::Printf(TEXT("event-%02d"), Index);
		const FString EventJson = FString::Printf(TEXT("{\"index\":%d}"), Index);
		TestTrue(*FString::Printf(TEXT("SaveEvent %d accepts write"), Index), Provider.SaveEvent(EventId, EventJson));
	}

	Provider.FlushPendingWrites();

	for (int32 Index = 0; Index < 10; ++Index)
	{
		const FString EventId = FString::Printf(TEXT("event-%02d"), Index);
		FString SavedJson;
		TestTrue(*FString::Printf(TEXT("Event %d file exists after flush"), Index), FFileHelper::LoadFileToString(SavedJson, *Fixture.GetEventFilePath(EventId)));
		TestEqual(*FString::Printf(TEXT("Event %d content matches"), Index), SavedJson, FString::Printf(TEXT("{\"index\":%d}"), Index));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFileStorageFlushWithNoPendingWritesReturnsImmediatelyTest, "UnrealHog.Storage.FileStorageProvider.FlushPendingWrites.WithNoPendingWritesReturnsImmediately", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFileStorageFlushWithNoPendingWritesReturnsImmediatelyTest::RunTest(const FString& Parameters)
{
	FScopedTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Provider(Fixture.GetRootPath());

	Provider.FlushPendingWrites();
	TestEqual(TEXT("No writes after empty flush"), Provider.GetEventCount(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFileStorageFlushCalledMultipleTimesDoesNotThrowTest, "UnrealHog.Storage.FileStorageProvider.FlushPendingWrites.CalledMultipleTimesDoesNotThrow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFileStorageFlushCalledMultipleTimesDoesNotThrowTest::RunTest(const FString& Parameters)
{
	FScopedTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Provider(Fixture.GetRootPath());

	TestTrue(TEXT("SaveEvent accepts write"), Provider.SaveEvent(TEXT("event-1"), TEXT("{}")));
	Provider.FlushPendingWrites();
	Provider.FlushPendingWrites();
	Provider.FlushPendingWrites();

	TestEqual(TEXT("Event count remains stable"), Provider.GetEventCount(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFileStorageClearEventsWaitsForPendingWritesBeforeClearingTest, "UnrealHog.Storage.FileStorageProvider.ClearEvents.WaitsForPendingWritesBeforeClearing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFileStorageClearEventsWaitsForPendingWritesBeforeClearingTest::RunTest(const FString& Parameters)
{
	FScopedTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Provider(Fixture.GetRootPath());

	TestTrue(TEXT("First SaveEvent accepts write"), Provider.SaveEvent(TEXT("event-1"), TEXT("{}")));
	TestTrue(TEXT("Second SaveEvent accepts write"), Provider.SaveEvent(TEXT("event-2"), TEXT("{}")));
	TestTrue(TEXT("ClearEvents accepts pending clear"), Provider.ClearEvents());
	Provider.FlushPendingWrites();

	TestFalse(TEXT("First event file absent"), FPaths::FileExists(Fixture.GetEventFilePath(TEXT("event-1"))));
	TestFalse(TEXT("Second event file absent"), FPaths::FileExists(Fixture.GetEventFilePath(TEXT("event-2"))));
	TestEqual(TEXT("No events remain"), Provider.GetEventCount(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFileStorageConcurrentSavesDoNotCorruptIndexTest, "UnrealHog.Storage.FileStorageProvider.ConcurrentSaves_DoNotCorruptIndex", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFileStorageConcurrentSavesDoNotCorruptIndexTest::RunTest(const FString& Parameters)
{
	FScopedTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Provider(Fixture.GetRootPath());
	std::atomic<int32> FailureCount{0};

	ParallelFor(100, [&Provider, &FailureCount](int32 Index)
	{
		const FString EventId = FString::Printf(TEXT("event-%03d"), Index);
		const FString EventJson = FString::Printf(TEXT("{\"index\":%d}"), Index);
		if (!Provider.SaveEvent(EventId, EventJson))
		{
			++FailureCount;
		}
	});

	Provider.FlushPendingWrites();
	TestEqual(TEXT("No concurrent save failures"), FailureCount.load(), 0);
	TestEqual(TEXT("All concurrent saves indexed"), Provider.GetEventCount(), 100);

	const TArray<FString> EventIds = Provider.GetEventIds();
	for (int32 Index = 0; Index < 100; ++Index)
	{
		TestTrue(*FString::Printf(TEXT("Contains event %d"), Index), EventIds.Contains(FString::Printf(TEXT("event-%03d"), Index)));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFileStorageConcurrentSavesAndLoadsDoNotCorruptTest, "UnrealHog.Storage.FileStorageProvider.ConcurrentSavesAndLoads_DoNotCorrupt", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFileStorageConcurrentSavesAndLoadsDoNotCorruptTest::RunTest(const FString& Parameters)
{
	FScopedTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Provider(Fixture.GetRootPath());
	std::atomic<int32> FailureCount{0};

	ParallelFor(50, [&Provider, &FailureCount](int32 Index)
	{
		const FString EventId = FString::Printf(TEXT("event-%03d"), Index);
		const FString EventJson = FString::Printf(TEXT("{\"index\":%d}"), Index);
		if (!Provider.SaveEvent(EventId, EventJson))
		{
			++FailureCount;
			return;
		}

		FString LoadedJson;
		if (!Provider.LoadEvent(EventId, LoadedJson) || LoadedJson != EventJson)
		{
			++FailureCount;
		}
	});

	Provider.FlushPendingWrites();
	TestEqual(TEXT("No concurrent save/load failures"), FailureCount.load(), 0);
	TestEqual(TEXT("All concurrent save/load events indexed"), Provider.GetEventCount(), 50);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFileStorageConcurrentSavesAndDeletesDoNotThrowTest, "UnrealHog.Storage.FileStorageProvider.ConcurrentSavesAndDeletes_DoNotThrow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFileStorageConcurrentSavesAndDeletesDoNotThrowTest::RunTest(const FString& Parameters)
{
	FScopedTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Provider(Fixture.GetRootPath());
	std::atomic<int32> FailureCount{0};

	for (int32 Index = 0; Index < 50; ++Index)
	{
		const FString EventId = FString::Printf(TEXT("event-%02d"), Index);
		TestTrue(*FString::Printf(TEXT("Seed SaveEvent %d accepts write"), Index), Provider.SaveEvent(EventId, TEXT("{}")));
	}
	Provider.FlushPendingWrites();

	ParallelFor(50, [&Provider, &FailureCount](int32 Index)
	{
		if ((Index % 2) == 0)
		{
			const FString EventId = FString::Printf(TEXT("event-%02d"), Index);
			if (!Provider.DeleteEvent(EventId))
			{
				++FailureCount;
			}
		}
	});

	Provider.FlushPendingWrites();
	TestEqual(TEXT("No concurrent delete failures"), FailureCount.load(), 0);
	TestEqual(TEXT("Only odd events remain"), Provider.GetEventCount(), 25);

	const TArray<FString> EventIds = Provider.GetEventIds();
	for (int32 Index = 0; Index < 50; ++Index)
	{
		const FString EventId = FString::Printf(TEXT("event-%02d"), Index);
		if ((Index % 2) == 0)
		{
			TestFalse(*FString::Printf(TEXT("Even event %d deleted"), Index), EventIds.Contains(EventId));
		}
		else
		{
			TestTrue(*FString::Printf(TEXT("Odd event %d remains"), Index), EventIds.Contains(EventId));
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
