// Trevor Eckhoff, 2026. All rights reserved.

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
	
	virtual bool SaveEvent(const FString& EventId, const FString& EventJson) = 0;
	bool SaveEvent(const FString& EventId, const TSharedRef<FJsonObject>& EventJsonObject);
	virtual bool LoadEvent(const FString& EventId, FString& EventJson) = 0;
	virtual bool DeleteEvent(const FString& EventId) = 0;
	virtual bool ClearEvents() = 0;
	
	virtual TArray<FString> GetEventIds() = 0;
	virtual int32 GetEventCount() = 0;
	
	virtual bool SaveState(const FString& StateKey, const FString& StateJson) = 0;
	bool SaveState(const FString& StateKey, const TSharedRef<FJsonObject>& StateJsonObject);
	virtual bool LoadState(const FString& StateKey, FString& StateJson) = 0;
	virtual bool DeleteState(const FString& StateKey) = 0;
	
private:
	static bool SerializeJsonObject(const TSharedRef<FJsonObject>& JsonObject, FString& OutJson);
};
