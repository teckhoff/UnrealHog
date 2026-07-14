// Trevor Eckhoff, 2026. All rights reserved.


#include "Storage/PostHogFileStorageProvider.h"

#include "HAL/FileManager.h"
#include "Logging/PostHogLogger.h"
#include "Logging/StructuredLog.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "SDK/PostHogSdkInfo.h"


FPostHogFileStorageProvider::FPostHogFileStorageProvider()
	: FileIoPipe(TEXT("PostHogFileStorageProvider"))
{
	BasePath = FPaths::Combine(FPaths::ProjectSavedDir(), PostHogSdkInfo::GetLibraryName());
	QueuePath = FPaths::Combine(BasePath, TEXT("Queue"));
	StatePath = FPaths::Combine(BasePath, TEXT("State"));

	InitializeDirectories();
	ScanDiskEventIds();
}

FPostHogFileStorageProvider::FPostHogFileStorageProvider(const FString& InBasePath)
	: FileIoPipe(TEXT("PostHogFileStorageProvider"))
{
	BasePath = FPaths::Combine(InBasePath, PostHogSdkInfo::GetLibraryName());
	QueuePath = FPaths::Combine(BasePath, TEXT("Queue"));
	StatePath = FPaths::Combine(BasePath, TEXT("State"));

	InitializeDirectories();
	ScanDiskEventIds();
}

FPostHogFileStorageProvider::~FPostHogFileStorageProvider()
{
	FlushPendingWrites();
}

bool FPostHogFileStorageProvider::SaveEvent(const FString& EventId, const FString& EventJson)
{
	if (EventId.IsEmpty())
	{
		UE_LOG(LogPostHog, Warning, TEXT("Cannot save PostHog event with empty event ID"));
		return false;
	}

	const FString EventFilePath = GetEventFilePath(EventId);
	{
		FScopeLock Lock(&StorageStateLock);
		const bool bWasPersisted = PersistedEventIds.Contains(EventId);
		EventIds.Add(EventId);
		const uint64 Generation = IncrementEventGenerationLocked(EventId);
		LaunchTrackedIoTask(UE_SOURCE_LOCATION, [this, EventId, EventJson, EventFilePath, Generation, bWasPersisted]()
		{
			{
				FScopeLock Lock(&StorageStateLock);
				const uint64* CurrentGeneration = EventGenerations.Find(EventId);
				if (CurrentGeneration == nullptr || *CurrentGeneration != Generation)
				{
					return;
				}
			}

			if (!FFileHelper::SaveStringToFile(EventJson, *EventFilePath))
			{
				UE_LOG(LogPostHog, Warning, TEXT("Failed to save PostHog event to %s"), *EventFilePath);
				FScopeLock Lock(&StorageStateLock);
				const uint64* CurrentGeneration = EventGenerations.Find(EventId);
				if (CurrentGeneration != nullptr && *CurrentGeneration == Generation)
				{
					if (bWasPersisted)
					{
						EventIds.Add(EventId);
					}
					else
					{
						EventIds.Remove(EventId);
						PersistedEventIds.Remove(EventId);
					}
				}
				return;
			}

			FScopeLock Lock(&StorageStateLock);
			const uint64* CurrentGeneration = EventGenerations.Find(EventId);
			if (CurrentGeneration != nullptr && *CurrentGeneration == Generation)
			{
				EventIds.Add(EventId);
				PersistedEventIds.Add(EventId);
			}
		});
	}

	return true;
}

bool FPostHogFileStorageProvider::LoadEvent(const FString& EventId, FString& EventJson)
{
	EventJson.Empty();

	if (EventId.IsEmpty())
	{
		UE_LOG(LogPostHog, Warning, TEXT("Cannot load PostHog event with empty event ID"));
		return false;
	}

	const FString EventFilePath = GetEventFilePath(EventId);
	bool bLoaded = false;
	FlushPendingWrites();
	RunBlockingIoTask(UE_SOURCE_LOCATION, [&EventJson, EventFilePath, &bLoaded]()
	{
		if (!FPaths::FileExists(EventFilePath))
		{
			return;
		}

		bLoaded = FFileHelper::LoadFileToString(EventJson, *EventFilePath);
		if (!bLoaded)
		{
			UE_LOG(LogPostHog, Warning, TEXT("Failed to load PostHog event from %s"), *EventFilePath);
			EventJson.Empty();
		}
	});

	return bLoaded;
}

bool FPostHogFileStorageProvider::DeleteEvent(const FString& EventId)
{
	if (EventId.IsEmpty())
	{
		UE_LOG(LogPostHog, Warning, TEXT("Cannot delete PostHog event with empty event ID"));
		return false;
	}

	const FString EventFilePath = GetEventFilePath(EventId);
	{
		FScopeLock Lock(&StorageStateLock);
		const bool bWasIndexed = EventIds.Contains(EventId);
		EventIds.Remove(EventId);
		const uint64 Generation = IncrementEventGenerationLocked(EventId);
		LaunchTrackedIoTask(UE_SOURCE_LOCATION, [this, EventId, EventFilePath, Generation, bWasIndexed]()
		{
			{
				FScopeLock Lock(&StorageStateLock);
				const uint64* CurrentGeneration = EventGenerations.Find(EventId);
				if (CurrentGeneration == nullptr || *CurrentGeneration != Generation)
				{
					return;
				}
			}

			if (DeleteFileIfExists(EventFilePath))
			{
				FScopeLock Lock(&StorageStateLock);
				const uint64* CurrentGeneration = EventGenerations.Find(EventId);
				if (CurrentGeneration != nullptr && *CurrentGeneration == Generation)
				{
					PersistedEventIds.Remove(EventId);
				}
			}
			else
			{
				FScopeLock Lock(&StorageStateLock);
				const uint64* CurrentGeneration = EventGenerations.Find(EventId);
				if (bWasIndexed && CurrentGeneration != nullptr && *CurrentGeneration == Generation)
				{
					EventIds.Add(EventId);
				}
			}
		});
	}

	return true;
}

bool FPostHogFileStorageProvider::ClearEvents()
{
	{
		FScopeLock Lock(&StorageStateLock);
		TArray<TPair<FString, uint64>> EventsToDelete;
		EventsToDelete.Reserve(EventIds.Num());
		for (const FString& EventId : EventIds)
		{
			EventsToDelete.Emplace(EventId, IncrementEventGenerationLocked(EventId));
		}
		EventIds.Empty();

		LaunchTrackedIoTask(UE_SOURCE_LOCATION, [this, EventsToDelete = MoveTemp(EventsToDelete)]()
		{
			for (const TPair<FString, uint64>& EventToDelete : EventsToDelete)
			{
				{
					FScopeLock Lock(&StorageStateLock);
					const uint64* CurrentGeneration = EventGenerations.Find(EventToDelete.Key);
					if (CurrentGeneration == nullptr || *CurrentGeneration != EventToDelete.Value)
					{
						continue;
					}
				}

				if (DeleteFileIfExists(GetEventFilePath(EventToDelete.Key)))
				{
					FScopeLock Lock(&StorageStateLock);
					const uint64* CurrentGeneration = EventGenerations.Find(EventToDelete.Key);
					if (CurrentGeneration != nullptr && *CurrentGeneration == EventToDelete.Value)
					{
						PersistedEventIds.Remove(EventToDelete.Key);
					}
				}
				else
				{
					FScopeLock Lock(&StorageStateLock);
					const uint64* CurrentGeneration = EventGenerations.Find(EventToDelete.Key);
					if (CurrentGeneration != nullptr && *CurrentGeneration == EventToDelete.Value)
					{
						EventIds.Add(EventToDelete.Key);
					}
				}
			}
		});
	}

	return true;
}

void FPostHogFileStorageProvider::FlushPendingWrites()
{
	while (true)
	{
		TArray<UE::Tasks::FTask> TasksToWait;
		{
			FScopeLock Lock(&StorageStateLock);
			PendingTasks.RemoveAll([](const UE::Tasks::FTask& Task)
			{
				return Task.IsCompleted();
			});

			if (PendingTasks.IsEmpty())
			{
				return;
			}

			TasksToWait = PendingTasks;
		}

		for (UE::Tasks::FTask& Task : TasksToWait)
		{
			Task.Wait();
		}
	}
}

TArray<FString> FPostHogFileStorageProvider::GetEventIds()
{
	TArray<FString> EventIds;
	{
		FScopeLock Lock(&StorageStateLock);
		this->EventIds.GenerateKeyArray(EventIds);
	}
	EventIds.Sort();

	return EventIds;
}

int32 FPostHogFileStorageProvider::GetEventCount()
{
	FScopeLock Lock(&StorageStateLock);
	return EventIds.Num();
}

bool FPostHogFileStorageProvider::SaveState(const FString& StateKey, const FString& StateJson)
{
	if (StateKey.IsEmpty())
	{
		UE_LOG(LogPostHog, Warning, TEXT("Cannot save PostHog state with empty state key"));
		return false;
	}

	const FString StateFilePath = GetStateFilePath(StateKey);
	{
		FScopeLock Lock(&StorageStateLock);
		const uint64 Generation = IncrementStateGenerationLocked(StateKey);
		LaunchTrackedIoTask(UE_SOURCE_LOCATION, [this, StateKey, StateJson, StateFilePath, Generation]()
		{
			{
				FScopeLock Lock(&StorageStateLock);
				const uint64* CurrentGeneration = StateGenerations.Find(StateKey);
				if (CurrentGeneration == nullptr || *CurrentGeneration != Generation)
				{
					return;
				}
			}

			if (!FFileHelper::SaveStringToFile(StateJson, *StateFilePath))
			{
				UE_LOG(LogPostHog, Warning, TEXT("Failed to save PostHog state to %s"), *StateFilePath);
			}
		});
	}

	return true;
}

bool FPostHogFileStorageProvider::LoadState(const FString& StateKey, FString& StateJson)
{
	StateJson.Empty();

	if (StateKey.IsEmpty())
	{
		UE_LOG(LogPostHog, Warning, TEXT("Cannot load PostHog state with empty state key"));
		return false;
	}

	const FString StateFilePath = GetStateFilePath(StateKey);
	bool bLoaded = false;
	FlushPendingWrites();
	RunBlockingIoTask(UE_SOURCE_LOCATION, [&StateJson, StateFilePath, &bLoaded]()
	{
		if (!FPaths::FileExists(StateFilePath))
		{
			return;
		}

		bLoaded = FFileHelper::LoadFileToString(StateJson, *StateFilePath);
		if (!bLoaded)
		{
			UE_LOG(LogPostHog, Warning, TEXT("Failed to load PostHog state from %s"), *StateFilePath);
			StateJson.Empty();
		}
	});

	return bLoaded;
}

bool FPostHogFileStorageProvider::DeleteState(const FString& StateKey)
{
	if (StateKey.IsEmpty())
	{
		UE_LOG(LogPostHog, Warning, TEXT("Cannot delete PostHog state with empty state key"));
		return false;
	}

	const FString StateFilePath = GetStateFilePath(StateKey);
	{
		FScopeLock Lock(&StorageStateLock);
		const uint64 Generation = IncrementStateGenerationLocked(StateKey);
		LaunchTrackedIoTask(UE_SOURCE_LOCATION, [this, StateKey, StateFilePath, Generation]()
		{
			{
				FScopeLock Lock(&StorageStateLock);
				const uint64* CurrentGeneration = StateGenerations.Find(StateKey);
				if (CurrentGeneration == nullptr || *CurrentGeneration != Generation)
				{
					return;
				}
			}

			DeleteFileIfExists(StateFilePath);
		});
	}

	return true;
}

void FPostHogFileStorageProvider::InitializeDirectories()
{
	UE_LOGFMT(LogPostHog, Log, "Using base path \"{BasePath}\".", BasePath);

	RunBlockingIoTask(UE_SOURCE_LOCATION, [this]()
	{
		IFileManager& FileManager = IFileManager::Get();
		const bool bQueueDirectoryReady = FileManager.MakeDirectory(*QueuePath, true);
		const bool bStateDirectoryReady = FileManager.MakeDirectory(*StatePath, true);

		if (!bQueueDirectoryReady)
		{
			UE_LOG(LogPostHog, Warning, TEXT("Failed to create PostHog queue directory %s"), *QueuePath);
		}

		if (!bStateDirectoryReady)
		{
			UE_LOG(LogPostHog, Warning, TEXT("Failed to create PostHog state directory %s"), *StatePath);
		}
	});
}

void FPostHogFileStorageProvider::ScanDiskEventIds()
{
	TArray<FString> DiskEventIds;
	RunBlockingIoTask(UE_SOURCE_LOCATION, [this, &DiskEventIds]()
	{
		TArray<FString> EventFiles;
		IFileManager::Get().FindFiles(EventFiles, *FPaths::Combine(QueuePath, TEXT("*.json")), true, false);

		DiskEventIds.Reserve(EventFiles.Num());
		for (const FString& EventFile : EventFiles)
		{
			DiskEventIds.Add(FPaths::GetBaseFilename(EventFile));
		}
	});

	FScopeLock Lock(&StorageStateLock);
	for (const FString& EventId : DiskEventIds)
	{
		EventIds.Add(EventId);
		PersistedEventIds.Add(EventId);
	}
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
		if (IFileManager::Get().DirectoryExists(*FilePath))
		{
			UE_LOG(LogPostHog, Warning, TEXT("PostHog file path is a directory and cannot be deleted as a file: %s"), *FilePath);
			return false;
		}

		return true;
	}

	const bool bDeleted = IFileManager::Get().Delete(*FilePath, false, true);
	if (!bDeleted)
	{
		UE_LOG(LogPostHog, Warning, TEXT("Failed to delete PostHog file %s"), *FilePath);
	}

	return bDeleted;
}

uint64 FPostHogFileStorageProvider::IncrementEventGenerationLocked(const FString& EventId)
{
	uint64& Generation = EventGenerations.FindOrAdd(EventId);
	++Generation;
	return Generation;
}

uint64 FPostHogFileStorageProvider::IncrementStateGenerationLocked(const FString& StateKey)
{
	uint64& Generation = StateGenerations.FindOrAdd(StateKey);
	++Generation;
	return Generation;
}

void FPostHogFileStorageProvider::LaunchTrackedIoTask(const TCHAR* DebugName, TFunction<void()>&& TaskBody)
{
	UE::Tasks::FTask Task = FileIoPipe.Launch(DebugName, [TaskBody = MoveTemp(TaskBody)]() mutable
	{
		TaskBody();
	});
	PendingTasks.Add(MoveTemp(Task));
}

void FPostHogFileStorageProvider::RunBlockingIoTask(const TCHAR* DebugName, TFunction<void()>&& TaskBody)
{
	UE::Tasks::FTask Task = FileIoPipe.Launch(DebugName, [TaskBody = MoveTemp(TaskBody)]() mutable
	{
		TaskBody();
	});
	Task.Wait();
}
