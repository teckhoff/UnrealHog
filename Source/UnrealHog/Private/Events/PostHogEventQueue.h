// Trevor Eckhoff, 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Events/PostHogEvent.h"
#include "Http.h"

class FPostHogHttpClient;
class IPostHogStorageProvider;
/**
 * 
 */
class FPostHogEventQueue
{
public:
	FPostHogEventQueue(IPostHogStorageProvider& InStorageProvider, 
		FPostHogHttpClient& InHttpClient, const FString& InApiKey, 
		int32 InMaxQueueSize, int32 InMaxBatchSize, int32 InFlushEventCount);
	~FPostHogEventQueue();
	
	bool Enqueue(const FPostHogEvent& Event);
	void Flush();
	void CancelInFlightRequest();
	int32 Num() const;
	
private:
	IPostHogStorageProvider& StorageProvider;
	FPostHogHttpClient& HttpClient;
	FString ApiKey;
	int32 MaxQueueSize;
	int32 MaxBatchSize;
	int32 FlushEventCount;
	
	TArray<FPostHogEvent> EventsQueue;
	TSet<FString> InFlightEventIds;
	FHttpRequestPtr ActiveRequest;
	bool bIsFlushing = false;
	
};
