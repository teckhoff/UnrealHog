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

enum class EPostHogEventQueueFlushResult : uint8
{
	Empty,
	Drained,
	Failed,
	Cancelled,
	ProgressBlocked
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

inline const TCHAR* LexToString(EPostHogEventQueueFlushResult Result)
{
	switch (Result)
	{
	case EPostHogEventQueueFlushResult::Empty:
		return TEXT("Empty");
	case EPostHogEventQueueFlushResult::Drained:
		return TEXT("Drained");
	case EPostHogEventQueueFlushResult::Failed:
		return TEXT("Failed");
	case EPostHogEventQueueFlushResult::Cancelled:
		return TEXT("Cancelled");
	case EPostHogEventQueueFlushResult::ProgressBlocked:
		return TEXT("ProgressBlocked");
	default:
		return TEXT("Unknown");
	}
}

using FPostHogEventQueueFlushComplete = TFunction<void(EPostHogEventQueueFlushResult Result)>;

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
	void Flush(FPostHogEventQueueFlushComplete OnComplete = {});
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
	TArray<FPostHogEventQueueFlushComplete> PendingFlushCallbacks;
	TSharedPtr<bool> FlushLifetimeToken;
	uint64 ActiveFlushGeneration = 0;
	int32 ActiveFlushInitialCount = 0;
	int32 ActiveFlushBatchCount = 0;
	bool bIsFlushing = false;

	struct FFlushBatch
	{
		TArray<FString> EventIds;
		TArray<FPostHogEvent> Events;
	};

	EPostHogEventQueueEnqueueResult EnsureCapacityForSave(const FString& IncomingEventId);
	bool TryGetOldestEvictableEventId(FString& OutEventId);
	bool TryLoadPersistedEventForBatch(const FString& EventId, FPostHogEvent& OutEvent, bool& bOutStopFlush);
	bool DeleteCorruptPersistedEvent(const FString& EventId, const FString& Reason);
	void ContinueFlush();
	bool TryBuildNextBatch(FFlushBatch& OutBatch, bool& bOutProgressBlocked);
	void SendBatch(FFlushBatch&& Batch);
	void HandleBatchComplete(uint64 Generation, const TArray<FString>& BatchEventIds, bool bSuccess, int32 StatusCode, const FString& ResponseBody);
	bool DeleteSentBatchRecords(const TArray<FString>& BatchEventIds);
	void CompleteFlush(EPostHogEventQueueFlushResult Result);
	static bool IsPermanentFailureStatus(int32 StatusCode);
};
