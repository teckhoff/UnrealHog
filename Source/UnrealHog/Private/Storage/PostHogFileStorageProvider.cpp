// Trevor Eckhoff, 2026. All rights reserved.


#include "Storage/PostHogFileStorageProvider.h"

#include "HAL/FileManager.h"
#include "Logging/PostHogLogger.h"
#include "Logging/StructuredLog.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "SDK/PostHogSdkInfo.h"


FPostHogFileStorageProvider::FPostHogFileStorageProvider()
{
	BasePath = FPaths::Combine(FPaths::ProjectSavedDir(), PostHogSdkInfo::GetLibraryName());
	
	InitializeDirectories();
}

FPostHogFileStorageProvider::FPostHogFileStorageProvider(const FString& InBasePath)
{
	BasePath = FPaths::Combine(InBasePath, PostHogSdkInfo::GetLibraryName());
	
	InitializeDirectories();
}

FPostHogFileStorageProvider::~FPostHogFileStorageProvider()
{
}

bool FPostHogFileStorageProvider::SaveEvent(const FString& EventId, const FString& EventJson)
{
	if (EventId.IsEmpty())
	{
		UE_LOG(LogPostHog, Warning, TEXT("Cannot save PostHog event with empty event ID"));
		return false;
	}
	
	InitializeDirectories();
	
	const FString EventFilePath = GetEventFilePath(EventId);
	const bool bSaved = FFileHelper::SaveStringToFile(EventJson, *EventFilePath);
	
	if (!bSaved)
	{
		UE_LOG(LogPostHog, Warning, TEXT("Failed to save PostHog event to %s"), *EventFilePath);
	}
	
	return bSaved;
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
		UE_LOG(LogPostHog, Warning, TEXT("Cannot delete PostHog event with empty event ID"));
		return false;
	}
	
	return DeleteFileIfExists(GetEventFilePath(EventId));
}

bool FPostHogFileStorageProvider::ClearEvents()
{
	bool bAllDeleted = true;
	
	for (const FString& EventId : GetEventIds())
	{
		bAllDeleted &= DeleteEvent(EventId);
	}
	
	return bAllDeleted;
}

TArray<FString> FPostHogFileStorageProvider::GetEventIds()
{
	TArray<FString> EventFiles;
	IFileManager::Get().FindFiles(EventFiles, *FPaths::Combine(QueuePath, TEXT("*.json")), true, false);
	EventFiles.Sort();
	
	TArray<FString> EventIds;
	EventIds.Reserve(EventFiles.Num());
	
	for (const FString& EventFile : EventFiles)
	{
		EventIds.Add(FPaths::GetBaseFilename(EventFile));
	}
	
	return EventIds;
}

int32 FPostHogFileStorageProvider::GetEventCount()
{
	return GetEventIds().Num();
}

bool FPostHogFileStorageProvider::SaveState(const FString& StateKey, const FString& StateJson)
{
	if (StateKey.IsEmpty())
	{
		UE_LOG(LogPostHog, Warning, TEXT("Cannot save PostHog state with empty state key"));
		return false;
	}
	
	InitializeDirectories();
	
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
		UE_LOG(LogPostHog, Warning, TEXT("Cannot load PostHog state with empty state key"));
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
		UE_LOG(LogPostHog, Warning, TEXT("Cannot delete PostHog state with empty state key"));
		return false;
	}
	
	return DeleteFileIfExists(GetStateFilePath(StateKey));
}

bool FPostHogFileStorageProvider::InitializeDirectories()
{
	UE_LOGFMT(LogPostHog, Log, "Using base path \"{BasePath}\".", BasePath);
	
	QueuePath = FPaths::Combine(BasePath, TEXT("Queue"));
	StatePath = FPaths::Combine(BasePath, TEXT("State"));
	
	IFileManager& FileManager = IFileManager::Get();
	
	const bool bQueueDirectoryReady = FileManager.MakeDirectory(*QueuePath, true);
	const bool bStateDirectoryReady = FileManager.MakeDirectory(*StatePath, true);

	return bQueueDirectoryReady && bStateDirectoryReady;
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
