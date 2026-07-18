#pragma once

#include "CoreMinimal.h"

class FJsonObject;
/**
 * 
 */
class UNREALHOG_API IPostHogStorageProvider
{
public:
	virtual ~IPostHogStorageProvider() = default;
	
	// I have a suspicion the FileStorageProvider might not work on all platforms.
	// Further testing is needed, but make an accessor just in case.
	static TUniquePtr<IPostHogStorageProvider> CreateDefaultProvider();
	
	// Returns false only when the event was not durably accepted; provider-visible indexes must
	// remain consistent with durable state after a false result.
	virtual bool SaveEvent(const FString& EventId, const FString& EventJson) = 0;
	bool SaveEvent(const FString& EventId, const TSharedRef<FJsonObject>& EventJsonObject);
	virtual bool LoadEvent(const FString& EventId, FString& EventJson) = 0;
	// Returns false only when deletion failed; provider-visible indexes must keep the event visible
	// if its durable record could not be removed.
	virtual bool DeleteEvent(const FString& EventId) = 0;
	virtual bool ClearEvents() = 0;
	
	// Oldest first by deterministic UUIDv7/lexical ordering.
	virtual TArray<FString> GetEventIds() = 0;
	virtual int32 GetEventCount() = 0;
	
	virtual bool SaveState(const FString& StateKey, const FString& StateJson) = 0;
	bool SaveState(const FString& StateKey, const TSharedRef<FJsonObject>& StateJsonObject);
	virtual bool LoadState(const FString& StateKey, FString& StateJson) = 0;
	virtual bool DeleteState(const FString& StateKey) = 0;

	// Blocks until any asynchronous durable writes queued by this provider have completed.
	// Default no-op for providers whose writes are already synchronous. Never issues network
	// requests; used by lifecycle shutdown/background paths that must not touch the network.
	virtual void FlushPendingWrites() {}

private:
	static bool SerializeJsonObject(const TSharedRef<FJsonObject>& JsonObject, FString& OutJson);
};
