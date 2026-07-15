
#include "Storage/PostHogFileStorageProvider.h"

#include "HAL/FileManager.h"
#include "Logging/PostHogLogger.h"
#include "Logging/StructuredLog.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "SDK/PostHogSdkInfo.h"


FPostHogFileStorageProvider::FPostHogFileStorageProvider()
	: WritePipe(TEXT("PostHogFileStorageProviderPipe"))
{
	BasePath = FPaths::Combine(FPaths::ProjectSavedDir(), FPostHogSdkInfo::GetLibraryName());
	QueuePath = FPaths::Combine(BasePath, TEXT("Queue"));
	StatePath = FPaths::Combine(BasePath, TEXT("State"));

	// Reads existing files only; must not create the Queue directory as a side effect of construction.
	LoadEventIndexFromDisk();
}

FPostHogFileStorageProvider::FPostHogFileStorageProvider(const FString& InBasePath)
	: WritePipe(TEXT("PostHogFileStorageProviderPipe"))
{
	BasePath = FPaths::Combine(InBasePath, FPostHogSdkInfo::GetLibraryName());
	QueuePath = FPaths::Combine(BasePath, TEXT("Queue"));
	StatePath = FPaths::Combine(BasePath, TEXT("State"));

	// Reads existing files only; must not create the Queue directory as a side effect of construction.
	LoadEventIndexFromDisk();
}

FPostHogFileStorageProvider::~FPostHogFileStorageProvider()
{
	// The pipe must be drained before destruction: queued lambdas capture `this`,
	// and FPipe's own destructor asserts that no work remains.
	WritePipe.WaitUntilEmpty();
}

bool FPostHogFileStorageProvider::SaveEvent(const FString& EventId, const FString& EventJson)
{
	if (EventId.IsEmpty())
	{
#if !WITH_DEV_AUTOMATION_TESTS
		UE_LOG(LogPostHog, Warning, TEXT("Cannot save PostHog event with empty event ID"));
#endif
		return false;
	}

	EnsureQueueDirectory();

	{
		FScopeLock Lock(&IndexLock);
		EventIdIndex.AddUnique(EventId);
		EventIdIndex.Sort();
	}

	const FString EventFilePath = GetEventFilePath(EventId);

	WritePipe.Launch(UE_SOURCE_LOCATION, [this, EventId, EventFilePath, EventJson]()
	{
		const bool bSaved = FFileHelper::SaveStringToFile(EventJson, *EventFilePath);

		if (!bSaved)
		{
			UE_LOG(LogPostHog, Warning, TEXT("Failed to save PostHog event to %s"), *EventFilePath);

			FScopeLock Lock(&IndexLock);
			EventIdIndex.Remove(EventId);
		}
	});

	return true;
}

bool FPostHogFileStorageProvider::LoadEvent(const FString& EventId, FString& EventJson)
{
	EventJson.Empty();

	if (EventId.IsEmpty())
	{
#if !WITH_DEV_AUTOMATION_TESTS
		UE_LOG(LogPostHog, Warning, TEXT("Cannot load PostHog event with empty event ID"));
#endif
		return false;
	}

	WritePipe.WaitUntilEmpty();

	const FString EventFilePath = GetEventFilePath(EventId);
	if (!FPaths::FileExists(EventFilePath))
	{
		return false;
	}

	const bool bLoaded = FFileHelper::LoadFileToString(EventJson, *EventFilePath);
	if (!bLoaded)
	{
		UE_LOG(LogPostHog, Warning, TEXT("Failed to load PostHog event from %s"), *EventFilePath);
		EventJson.Empty();
	}

	return bLoaded;
}

bool FPostHogFileStorageProvider::DeleteEvent(const FString& EventId)
{
	if (EventId.IsEmpty())
	{
#if !WITH_DEV_AUTOMATION_TESTS
		UE_LOG(LogPostHog, Warning, TEXT("Cannot delete PostHog event with empty event ID"));
#endif
		return false;
	}

	WritePipe.WaitUntilEmpty();

	{
		FScopeLock Lock(&IndexLock);
		EventIdIndex.Remove(EventId);
	}

	return DeleteFileIfExists(GetEventFilePath(EventId));
}

bool FPostHogFileStorageProvider::ClearEvents()
{
	FlushPendingWrites();

	TArray<FString> EventIdsToDelete;
	{
		FScopeLock Lock(&IndexLock);
		EventIdsToDelete = MoveTemp(EventIdIndex);
		EventIdIndex.Reset();
	}

	bool bAllDeleted = true;

	for (const FString& EventId : EventIdsToDelete)
	{
		bAllDeleted &= DeleteFileIfExists(GetEventFilePath(EventId));
	}

	return bAllDeleted;
}

TArray<FString> FPostHogFileStorageProvider::GetEventIds()
{
	FScopeLock Lock(&IndexLock);
	return EventIdIndex;
}

int32 FPostHogFileStorageProvider::GetEventCount()
{
	FScopeLock Lock(&IndexLock);
	return EventIdIndex.Num();
}

void FPostHogFileStorageProvider::FlushPendingWrites()
{
	WritePipe.WaitUntilEmpty();
}

bool FPostHogFileStorageProvider::SaveState(const FString& StateKey, const FString& StateJson)
{
	if (StateKey.IsEmpty())
	{
#if !WITH_DEV_AUTOMATION_TESTS
		UE_LOG(LogPostHog, Warning, TEXT("Cannot save PostHog state with empty state key"));
#endif 
		return false;
	}
	
	EnsureStateDirectory();

	const FString StateFilePath = GetStateFilePath(StateKey);
	const bool bSaved = FFileHelper::SaveStringToFile(StateJson, *StateFilePath);
	
	if (!bSaved)
	{
		UE_LOG(LogPostHog, Warning, TEXT("Failed to save PostHog state to %s"), *StateFilePath);
	}
	
	return bSaved;
}

bool FPostHogFileStorageProvider::LoadState(const FString& StateKey, FString& StateJson)
{
	StateJson.Empty();
	
	if (StateKey.IsEmpty())
	{
#if !WITH_DEV_AUTOMATION_TESTS
		UE_LOG(LogPostHog, Warning, TEXT("Cannot load PostHog state with empty state key"));
#endif
		return false;
	}
	
	const FString StateFilePath = GetStateFilePath(StateKey);
	if (!FPaths::FileExists(StateFilePath))
	{
		return false;
	}
	
	const bool bLoaded = FFileHelper::LoadFileToString(StateJson, *StateFilePath);
	if (!bLoaded)
	{
		UE_LOG(LogPostHog, Warning, TEXT("Failed to load PostHog state from %s"), *StateFilePath);
		StateJson.Empty();
	}
	
	return bLoaded;
}

bool FPostHogFileStorageProvider::DeleteState(const FString& StateKey)
{
	if (StateKey.IsEmpty())
	{
#if !WITH_DEV_AUTOMATION_TESTS
		UE_LOG(LogPostHog, Warning, TEXT("Cannot delete PostHog state with empty state key"));
#endif
		return false;
	}
	
	return DeleteFileIfExists(GetStateFilePath(StateKey));
}

void FPostHogFileStorageProvider::LoadEventIndexFromDisk()
{
	TArray<FString> EventFiles;
	IFileManager::Get().FindFiles(EventFiles, *FPaths::Combine(QueuePath, TEXT("*.json")), true, false);
	EventFiles.Sort();

	FScopeLock Lock(&IndexLock);
	EventIdIndex.Reset(EventFiles.Num());

	for (const FString& EventFile : EventFiles)
	{
		EventIdIndex.Add(FPaths::GetBaseFilename(EventFile));
	}
}

void FPostHogFileStorageProvider::EnsureQueueDirectory()
{
	FScopeLock Lock(&DirectoryLock);
	if (bQueueDirectoryReady)
	{
		return;
	}

	UE_LOGFMT(LogPostHog, Log, "Using base path \"{BasePath}\".", BasePath);
	bQueueDirectoryReady = IFileManager::Get().MakeDirectory(*QueuePath, true);
}

void FPostHogFileStorageProvider::EnsureStateDirectory()
{
	FScopeLock Lock(&DirectoryLock);
	if (bStateDirectoryReady)
	{
		return;
	}

	UE_LOGFMT(LogPostHog, Log, "Using base path \"{BasePath}\".", BasePath);
	bStateDirectoryReady = IFileManager::Get().MakeDirectory(*StatePath, true);
}

FString FPostHogFileStorageProvider::GetEventFilePath(const FString& EventId) const
{
	return FPaths::Combine(QueuePath, MakeJsonFileName(EventId));
}

FString FPostHogFileStorageProvider::GetStateFilePath(const FString& StateKey) const
{
	return FPaths::Combine(StatePath, MakeJsonFileName(StateKey));
}

FString FPostHogFileStorageProvider::MakeJsonFileName(const FString& Key)
{
	return FString::Printf(TEXT("%s.json"), *Key);
}

bool FPostHogFileStorageProvider::DeleteFileIfExists(const FString& FilePath)
{
	if (!FPaths::FileExists(FilePath))
	{
		return true;
	}
	
	const bool bDeleted = IFileManager::Get().Delete(*FilePath, false, true);
	if (!bDeleted)
	{
		UE_LOG(LogPostHog, Warning, TEXT("Failed to delete PostHog file %s"), *FilePath);
	}
	
	return bDeleted;
}
