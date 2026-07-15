// Trevor Eckhoff, 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Events/PostHogEvent.h"
#include "Http/PostHogBatchTransport.h"

class IPostHogStorageProvider;
/**
 *
 */
class FPostHogEventQueue
{
public:
	FPostHogEventQueue(IPostHogStorageProvider& InStorageProvider,
		IPostHogBatchTransport& InTransport, const FString& InApiKey,
		int32 InMaxQueueSize, int32 InMaxBatchSize, int32 InFlushEventCount);
	~FPostHogEventQueue();

	bool Enqueue(const FPostHogEvent& Event);
	void Flush();
	void CancelInFlightRequest();

	// Cancels any in-flight send, clears all persisted and in-memory queued events.
	void Clear();

	int32 Num() const;

private:
	IPostHogStorageProvider& StorageProvider;
	IPostHogBatchTransport& Transport;
	FString ApiKey;
	int32 MaxQueueSize;
	int32 MaxBatchSize;
	int32 FlushEventCount;

	TArray<FPostHogEvent> EventsQueue;
	TSet<FString> InFlightEventIds;
	TSharedPtr<IPostHogBatchRequestHandle> ActiveRequestHandle;
	bool bIsFlushing = false;

};
