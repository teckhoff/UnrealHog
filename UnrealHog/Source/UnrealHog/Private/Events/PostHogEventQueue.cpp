
#include "Events/PostHogEventQueue.h"

#include "Dom/JsonObject.h"
#include "Events/PostHogBatchPayload.h"
#include "Events/PostHogEvent.h"
#include "Logging/PostHogLogger.h"
#include "Logging/StructuredLog.h"
#include "Serialization/JsonSerializer.h"
#include "Storage/PostHogStorageProvider.h"
#include "Time/PostHogClock.h"

FPostHogEventQueue::FPostHogEventQueue(IPostHogStorageProvider& InStorageProvider, IPostHogBatchTransport& InTransport,
	const FString& InApiKey, int32 InMaxQueueSize, int32 InMaxBatchSize, int32 InFlushEventCount,
	IPostHogClock* InClock) :
	StorageProvider(InStorageProvider),
	Transport(InTransport),
	ApiKey(InApiKey),
	MaxQueueSize(FMath::Max(InMaxQueueSize, 1)),
	AdjustedMaxBatchSize(FMath::Max(InMaxBatchSize, 1)),
	AdjustedFlushEventCount(FMath::Max(InFlushEventCount, 1)),
	FlushLifetimeToken(MakeShared<bool>(true)),
	OwnedClock(InClock ? nullptr : MakeUnique<FPostHogSystemClock>()),
	Clock(InClock ? *InClock : *OwnedClock)
{
}

FPostHogEventQueue::~FPostHogEventQueue()
{
	if (FlushLifetimeToken.IsValid())
	{
		*FlushLifetimeToken = false;
	}

	CancelInFlightRequest();
	FlushLifetimeToken.Reset();
}

EPostHogEventQueueEnqueueResult FPostHogEventQueue::Enqueue(const FPostHogEvent& Event)
{
	const FString IncomingEventId = Event.GetEventId();
	const EPostHogEventQueueEnqueueResult CapacityResult = EnsureCapacityForSave(IncomingEventId);
	if (CapacityResult != EPostHogEventQueueEnqueueResult::Enqueued)
	{
		return CapacityResult;
	}

	if (!StorageProvider.SaveEvent(IncomingEventId, Event.ToJsonObject()))
	{
		UE_LOGFMT(LogPostHog, Warning, "PostHog event queue rejected incoming event because SaveEvent failed. IncomingEventId={0}, MaxQueueSize={1}, CurrentCount={2}.",
			IncomingEventId, MaxQueueSize, StorageProvider.GetEventCount());
		return EPostHogEventQueueEnqueueResult::RejectedSaveFailed;
	}

	if (StorageProvider.GetEventCount() >= AdjustedFlushEventCount)
	{
		Flush();
	}

	return EPostHogEventQueueEnqueueResult::Enqueued;
}

EPostHogEventQueueEnqueueResult FPostHogEventQueue::EnsureCapacityForSave(const FString& IncomingEventId)
{
	while (StorageProvider.GetEventCount() >= MaxQueueSize)
	{
		const int32 CurrentCount = StorageProvider.GetEventCount();

		FString EventIdToDrop;
		if (!TryGetOldestEvictableEventId(EventIdToDrop))
		{
			UE_LOGFMT(LogPostHog, Warning, "PostHog event queue rejected incoming event because every persisted record is in flight. IncomingEventId={0}, MaxQueueSize={1}, CurrentCount={2}.",
				IncomingEventId, MaxQueueSize, CurrentCount);
			return EPostHogEventQueueEnqueueResult::RejectedCapacityNoEvictableEvent;
		}

		if (!StorageProvider.DeleteEvent(EventIdToDrop))
		{
			UE_LOGFMT(LogPostHog, Warning, "PostHog event queue rejected incoming event because capacity eviction failed. IncomingEventId={0}, DroppedEventId={1}, MaxQueueSize={2}, CurrentCount={3}.",
				IncomingEventId, EventIdToDrop, MaxQueueSize, CurrentCount);
			return EPostHogEventQueueEnqueueResult::RejectedCapacityDeleteFailed;
		}

		const int32 CountAfterDelete = StorageProvider.GetEventCount();
		if (CountAfterDelete >= CurrentCount)
		{
			UE_LOGFMT(LogPostHog, Warning, "PostHog event queue rejected incoming event because capacity eviction did not reduce persisted count. IncomingEventId={0}, DroppedEventId={1}, MaxQueueSize={2}, CurrentCount={3}.",
				IncomingEventId, EventIdToDrop, MaxQueueSize, CountAfterDelete);
			return EPostHogEventQueueEnqueueResult::RejectedCapacityDeleteFailed;
		}

		UE_LOGFMT(LogPostHog, Log, "PostHog event queue dropped oldest persisted event before enqueue. IncomingEventId={0}, DroppedEventId={1}, MaxQueueSize={2}, CurrentCount={3}.",
			IncomingEventId, EventIdToDrop, MaxQueueSize, CurrentCount);
	}

	return EPostHogEventQueueEnqueueResult::Enqueued;
}

bool FPostHogEventQueue::TryGetOldestEvictableEventId(FString& OutEventId)
{
	const TArray<FString> EventIds = StorageProvider.GetEventIds();
	for (const FString& EventId : EventIds)
	{
		if (!InFlightEventIds.Contains(EventId))
		{
			OutEventId = EventId;
			return true;
		}
	}

	OutEventId.Empty();
	return false;
}

bool FPostHogEventQueue::TryLoadPersistedEventForBatch(const FString& EventId, FPostHogEvent& OutEvent, bool& bOutStopFlush)
{
	bOutStopFlush = false;

	FString EventJson;
	if (!StorageProvider.LoadEvent(EventId, EventJson))
	{
		bOutStopFlush = !DeleteCorruptPersistedEvent(EventId, TEXT("LoadEvent failed."));
		return false;
	}

	TSharedPtr<FJsonObject> EventJsonObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(EventJson);
	if (!FJsonSerializer::Deserialize(Reader, EventJsonObject) || !EventJsonObject.IsValid())
	{
		bOutStopFlush = !DeleteCorruptPersistedEvent(EventId, TEXT("Malformed JSON."));
		return false;
	}

	FString ParseError;
	TOptional<FPostHogEvent> ParsedEvent = FPostHogEvent::TryParseFromJson(EventJsonObject.ToSharedRef(), ParseError);
	if (!ParsedEvent.IsSet())
	{
		bOutStopFlush = !DeleteCorruptPersistedEvent(EventId, ParseError);
		return false;
	}

	OutEvent = ParsedEvent.GetValue();
	return true;
}

bool FPostHogEventQueue::DeleteCorruptPersistedEvent(const FString& EventId, const FString& Reason)
{
	const int32 CountBeforeDelete = StorageProvider.GetEventCount();
	if (!StorageProvider.DeleteEvent(EventId))
	{
		UE_LOGFMT(LogPostHog, Warning, "PostHog event queue failed to delete corrupt persisted event; stopping flush. EventId={0}, Reason={1}.",
			EventId, Reason);
		return false;
	}

	const int32 CountAfterDelete = StorageProvider.GetEventCount();
	if (CountAfterDelete >= CountBeforeDelete)
	{
		UE_LOGFMT(LogPostHog, Warning, "PostHog event queue failed to make progress deleting corrupt persisted event; stopping flush. EventId={0}, Reason={1}, CountBeforeDelete={2}, CountAfterDelete={3}.",
			EventId, Reason, CountBeforeDelete, CountAfterDelete);
		return false;
	}

	UE_LOGFMT(LogPostHog, Warning, "PostHog event queue deleted corrupt persisted event. EventId={0}, Reason={1}.",
		EventId, Reason);
	return true;
}

void FPostHogEventQueue::Flush(FPostHogEventQueueFlushComplete OnComplete)
{
	if (bIsFlushing)
	{
		if (OnComplete)
		{
			PendingFlushCallbacks.Add(MoveTemp(OnComplete));
		}
		return;
	}

	if (PausedUntil.IsSet() && Clock.UtcNow() < PausedUntil.GetValue())
	{
		if (OnComplete)
		{
			OnComplete(EPostHogEventQueueFlushResult::Paused);
		}
		return;
	}

	if (OnComplete)
	{
		PendingFlushCallbacks.Add(MoveTemp(OnComplete));
	}

	bIsFlushing = true;
	ActiveRequestHandle.Reset();
	InFlightEventIds.Reset();
	++ActiveFlushGeneration;
	ActiveFlushInitialCount = StorageProvider.GetEventCount();
	ActiveFlushBatchCount = 0;

	ContinueFlush();
}

void FPostHogEventQueue::ContinueFlush()
{
	if (!bIsFlushing)
	{
		return;
	}

	const int32 CountBeforeBuild = StorageProvider.GetEventCount();
	if (CountBeforeBuild == 0)
	{
		CompleteFlush(ActiveFlushInitialCount == 0 ? EPostHogEventQueueFlushResult::Empty : EPostHogEventQueueFlushResult::Drained);
		return;
	}

	FFlushBatch Batch;
	bool bProgressBlocked = false;
	if (!TryBuildNextBatch(Batch, bProgressBlocked))
	{
		CompleteFlush(EPostHogEventQueueFlushResult::ProgressBlocked);
		return;
	}

	if (Batch.Events.Num() == 0)
	{
		const int32 CountAfterBuild = StorageProvider.GetEventCount();
		if (CountAfterBuild == 0)
		{
			CompleteFlush(ActiveFlushInitialCount == 0 ? EPostHogEventQueueFlushResult::Empty : EPostHogEventQueueFlushResult::Drained);
			return;
		}

		if (bProgressBlocked || CountAfterBuild >= CountBeforeBuild)
		{
			CompleteFlush(EPostHogEventQueueFlushResult::ProgressBlocked);
			return;
		}

		ContinueFlush();
		return;
	}

	SendBatch(MoveTemp(Batch));
}

bool FPostHogEventQueue::TryBuildNextBatch(FFlushBatch& OutBatch, bool& bOutProgressBlocked)
{
	bOutProgressBlocked = false;

	const TArray<FString> EventIds = StorageProvider.GetEventIds();
	const int32 BatchCapacity = FMath::Min(AdjustedMaxBatchSize, EventIds.Num());
	OutBatch.Events.Reset(BatchCapacity);
	OutBatch.EventIds.Reset(BatchCapacity);

	for (const FString& EventId : EventIds)
	{
		if (OutBatch.Events.Num() >= AdjustedMaxBatchSize)
		{
			break;
		}

		FPostHogEvent Event(TEXT(""), TEXT(""));
		bool bStopFlush = false;
		if (!TryLoadPersistedEventForBatch(EventId, Event, bStopFlush))
		{
			if (bStopFlush)
			{
				bOutProgressBlocked = true;
				return false;
			}

			continue;
		}

		OutBatch.Events.Add(Event);
		OutBatch.EventIds.Add(EventId);
	}

	return true;
}

void FPostHogEventQueue::SendBatch(FFlushBatch&& Batch)
{
	if (!bIsFlushing || Batch.Events.Num() == 0)
	{
		return;
	}

	for (const FString& EventId : Batch.EventIds)
	{
		InFlightEventIds.Add(EventId);
	}

	const uint64 Generation = ActiveFlushGeneration;
	const TArray<FString> BatchEventIds = MoveTemp(Batch.EventIds);
	const FPostHogBatchPayload Payload(ApiKey, Batch.Events);
	const TWeakPtr<bool> WeakLifetime = FlushLifetimeToken;
	FPostHogEventQueue* Queue = this;
	++ActiveFlushBatchCount;

	const TSharedPtr<IPostHogBatchRequestHandle> RequestHandle = Transport.SendBatch(Payload, [WeakLifetime, Queue, Generation, BatchEventIds](bool bSuccess, int32 StatusCode, const FString& ResponseBody)
	{
		const TSharedPtr<bool> Lifetime = WeakLifetime.Pin();
		if (!Lifetime.IsValid() || !*Lifetime)
		{
			return;
		}

		if (!Queue || Queue->ActiveFlushGeneration != Generation)
		{
			return;
		}

		Queue->HandleBatchComplete(Generation, BatchEventIds, bSuccess, StatusCode, ResponseBody);
	});

	if (!bIsFlushing || ActiveFlushGeneration != Generation)
	{
		return;
	}

	ActiveRequestHandle = RequestHandle;
	if (!ActiveRequestHandle.IsValid())
	{
		HandleBatchComplete(Generation, BatchEventIds, false, 0, TEXT(""));
	}
}

void FPostHogEventQueue::HandleBatchComplete(uint64 Generation, const TArray<FString>& BatchEventIds, bool bSuccess, int32 StatusCode, const FString& ResponseBody)
{
	(void)ResponseBody;

	if (!bIsFlushing || ActiveFlushGeneration != Generation)
	{
		return;
	}

	ActiveRequestHandle.Reset();
	for (const FString& EventId : BatchEventIds)
	{
		InFlightEventIds.Remove(EventId);
	}

	if (!bSuccess)
	{
		if (IsPermanentFailureStatus(StatusCode))
		{
			UE_LOGFMT(LogPostHog, Warning, "PostHog event queue classified batch delivery failure as permanent; deleting attempted batch and ending flush. StatusCode={0}, BatchEventCount={1}.",
				StatusCode, BatchEventIds.Num());
			DeleteSentBatchRecords(BatchEventIds);
			CompleteFlush(EPostHogEventQueueFlushResult::Failed);
			return;
		}

		if (StatusCode == 413)
		{
			ReduceBatchLimitsAfterPayloadTooLarge();
		}

		UE_LOGFMT(LogPostHog, Warning, "PostHog event queue classified batch delivery failure as retryable; retaining attempted batch. StatusCode={0}.",
			StatusCode);
		++ConsecutiveRetryableFailures;
		const int32 DelaySeconds = FMath::Min(ConsecutiveRetryableFailures * RetryDelaySeconds, MaxRetryDelaySeconds);
		PausedUntil = Clock.UtcNow() + FTimespan::FromSeconds(DelaySeconds);
		CompleteFlush(EPostHogEventQueueFlushResult::Failed);
		return;
	}

	ConsecutiveRetryableFailures = 0;
	PausedUntil.Reset();

	if (!DeleteSentBatchRecords(BatchEventIds))
	{
		CompleteFlush(EPostHogEventQueueFlushResult::ProgressBlocked);
		return;
	}

	ContinueFlush();
}

bool FPostHogEventQueue::IsPermanentFailureStatus(int32 StatusCode)
{
	return StatusCode >= 400 && StatusCode < 500 && StatusCode != 413;
}

void FPostHogEventQueue::ReduceBatchLimitsAfterPayloadTooLarge()
{
	AdjustedMaxBatchSize = FMath::Max(AdjustedMaxBatchSize / 2, 1);
	AdjustedFlushEventCount = FMath::Max(AdjustedFlushEventCount / 2, 1);

	UE_LOGFMT(LogPostHog, Warning, "PostHog event queue received HTTP 413; reducing local batch limits. AdjustedMaxBatchSize={0}, AdjustedFlushEventCount={1}.",
		AdjustedMaxBatchSize, AdjustedFlushEventCount);
}

bool FPostHogEventQueue::DeleteSentBatchRecords(const TArray<FString>& BatchEventIds)
{
	for (const FString& EventId : BatchEventIds)
	{
		const int32 CountBeforeDelete = StorageProvider.GetEventCount();
		if (!StorageProvider.DeleteEvent(EventId))
		{
			UE_LOGFMT(LogPostHog, Warning, "PostHog event queue stopped flush because deleting a sent event failed. EventId={0}.", EventId);
			return false;
		}

		const int32 CountAfterDelete = StorageProvider.GetEventCount();
		if (CountAfterDelete >= CountBeforeDelete)
		{
			UE_LOGFMT(LogPostHog, Warning, "PostHog event queue stopped flush because deleting a sent event made no progress. EventId={0}, CountBeforeDelete={1}, CountAfterDelete={2}.",
				EventId, CountBeforeDelete, CountAfterDelete);
			return false;
		}
	}

	return true;
}

void FPostHogEventQueue::CompleteFlush(EPostHogEventQueueFlushResult Result)
{
	ActiveRequestHandle.Reset();
	InFlightEventIds.Reset();
	bIsFlushing = false;
	ActiveFlushInitialCount = 0;
	ActiveFlushBatchCount = 0;

	TArray<FPostHogEventQueueFlushComplete> Callbacks = MoveTemp(PendingFlushCallbacks);
	PendingFlushCallbacks.Reset();
	for (FPostHogEventQueueFlushComplete& Callback : Callbacks)
	{
		if (Callback)
		{
			Callback(Result);
		}
	}
}

void FPostHogEventQueue::CancelInFlightRequest()
{
	if (!bIsFlushing && !ActiveRequestHandle.IsValid())
	{
		return;
	}

	++ActiveFlushGeneration;
	const TSharedPtr<IPostHogBatchRequestHandle> RequestHandle = ActiveRequestHandle;
	ActiveRequestHandle.Reset();
	InFlightEventIds.Reset();

	if (RequestHandle.IsValid())
	{
		RequestHandle->Cancel();
	}

	CompleteFlush(EPostHogEventQueueFlushResult::Cancelled);
}

void FPostHogEventQueue::Clear()
{
	CancelInFlightRequest();
	StorageProvider.ClearEvents();
}

int32 FPostHogEventQueue::Num() const
{
	return StorageProvider.GetEventCount();
}
