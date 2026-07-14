// Trevor Eckhoff, 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Storage/PostHogStorageProvider.h"
#include "HAL/CriticalSection.h"
#include "Tasks/Pipe.h"

/**
 * File-backed storage provider for persisted PostHog events.
 * Event file I/O is performed asynchronously on a background UE::Tasks::FPipe;
 * state file I/O remains synchronous, matching the Unity reference implementation.
 */
class FPostHogFileStorageProvider : public IPostHogStorageProvider
{
public:
	FPostHogFileStorageProvider();
	FPostHogFileStorageProvider(const FString& InBasePath);
	virtual ~FPostHogFileStorageProvider() override;

	virtual bool SaveEvent(const FString& EventId, const FString& EventJson) override;
	using IPostHogStorageProvider::SaveEvent;
	virtual bool LoadEvent(const FString& EventId, FString& EventJson) override;
	virtual bool DeleteEvent(const FString& EventId) override;
	virtual bool ClearEvents() override;

	virtual TArray<FString> GetEventIds() override;
	virtual int32 GetEventCount() override;

	virtual bool SaveState(const FString& StateKey, const FString& StateJson) override;
	using IPostHogStorageProvider::SaveState;
	virtual bool LoadState(const FString& StateKey, FString& StateJson) override;
	virtual bool DeleteState(const FString& StateKey) override;

	/** Blocks the calling thread until all queued event writes have completed. Not part of IPostHogStorageProvider, matching Unity's FileStorageProvider.FlushPendingWrites(). */
	void FlushPendingWrites();

private:
	FString BasePath;
	FString QueuePath;
	FString StatePath;

	UE::Tasks::FPipe WritePipe;
	mutable FCriticalSection IndexLock;
	TArray<FString> EventIdIndex;

	bool InitializeDirectories();
	void LoadEventIndexFromDisk();
	FString GetEventFilePath(const FString& EventId) const;
	FString GetStateFilePath(const FString& StateKey) const;
	static FString MakeJsonFileName(const FString& Key);
	static bool DeleteFileIfExists(const FString& FilePath);
};
