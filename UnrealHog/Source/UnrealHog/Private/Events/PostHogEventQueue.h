#pragma once

#include "CoreMinimal.h"
#include "Events/PostHogEvent.h"
#include "Http/PostHogBatchTransport.h"

class IPostHogStorageProvider;

enum class EPostHogEventQueueEnqueueResult : uint8
{
	Enqueued,
	RejectedCapacityNoEvictableEvent,
	RejectedCapacityDeleteFailed,
	RejectedSaveFailed
};

inline const TCHAR* LexToString(EPostHogEventQueueEnqueueResult Result)
{
	switch (Result)
	{
	case EPostHogEventQueueEnqueueResult::Enqueued:
		return TEXT("Enqueued");
	case EPostHogEventQueueEnqueueResult::RejectedCapacityNoEvictableEvent:
		return TEXT("RejectedCapacityNoEvictableEvent");
	case EPostHogEventQueueEnqueueResult::RejectedCapacityDeleteFailed:
		return TEXT("RejectedCapacityDeleteFailed");
	case EPostHogEventQueueEnqueueResult::RejectedSaveFailed:
		return TEXT("RejectedSaveFailed");
	default:
		return TEXT("Unknown");
	}
}

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

	EPostHogEventQueueEnqueueResult Enqueue(const FPostHogEvent& Event);
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

	TSet<FString> InFlightEventIds;
	TSharedPtr<IPostHogBatchRequestHandle> ActiveRequestHandle;
	bool bIsFlushing = false;

	EPostHogEventQueueEnqueueResult EnsureCapacityForSave(const FString& IncomingEventId);
	bool TryGetOldestEvictableEventId(FString& OutEventId);
	bool TryLoadPersistedEventForBatch(const FString& EventId, FPostHogEvent& OutEvent, bool& bOutStopFlush);
	bool DeleteCorruptPersistedEvent(const FString& EventId, const FString& Reason);
};
