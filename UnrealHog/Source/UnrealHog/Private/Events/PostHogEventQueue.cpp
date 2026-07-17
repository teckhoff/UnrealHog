
#include "Events/PostHogEventQueue.h"

#include "Dom/JsonObject.h"
#include "Events/PostHogBatchPayload.h"
#include "Events/PostHogEvent.h"
#include "Logging/PostHogLogger.h"
#include "Logging/StructuredLog.h"
#include "Serialization/JsonSerializer.h"
#include "Storage/PostHogStorageProvider.h"

FPostHogEventQueue::FPostHogEventQueue(IPostHogStorageProvider& InStorageProvider, IPostHogBatchTransport& InTransport,
	const FString& InApiKey, int32 InMaxQueueSize, int32 InMaxBatchSize, int32 InFlushEventCount) :
	StorageProvider(InStorageProvider),
	Transport(InTransport),
	ApiKey(InApiKey),
	MaxQueueSize(FMath::Max(InMaxQueueSize, 1)), 
	MaxBatchSize(FMath::Max(InMaxBatchSize, 1)),
	FlushEventCount(FMath::Max(InFlushEventCount, 1))
{
}

FPostHogEventQueue::~FPostHogEventQueue()
{
	CancelInFlightRequest();
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

	if (StorageProvider.GetEventCount() >= FlushEventCount)
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
	if (!StorageProvider.DeleteEvent(EventId))
	{
		UE_LOGFMT(LogPostHog, Warning, "PostHog event queue failed to delete corrupt persisted event; stopping flush. EventId={0}, Reason={1}.",
			EventId, Reason);
		return false;
	}

	UE_LOGFMT(LogPostHog, Warning, "PostHog event queue deleted corrupt persisted event. EventId={0}, Reason={1}.",
		EventId, Reason);
	return true;
}

void FPostHogEventQueue::Flush()
{
	if (bIsFlushing)
	{
		return;
	}

	const TArray<FString> EventIds = StorageProvider.GetEventIds();
	if (EventIds.Num() == 0)
	{
		return;
	}

	TArray<FPostHogEvent> BatchEvents;
	BatchEvents.Reserve(MaxBatchSize);

	TArray<FString> BatchEventIds;
	BatchEventIds.Reserve(MaxBatchSize);

	for (const FString& EventId : EventIds)
	{
		if (BatchEvents.Num() >= MaxBatchSize)
		{
			break;
		}

		FPostHogEvent Event(TEXT(""), TEXT(""));
		bool bStopFlush = false;
		if (!TryLoadPersistedEventForBatch(EventId, Event, bStopFlush))
		{
			if (bStopFlush)
			{
				return;
			}

			continue;
		}

		BatchEvents.Add(Event);
		BatchEventIds.Add(EventId);
	}

	if (BatchEvents.Num() == 0)
	{
		return;
	}

	for (const FString& EventId : BatchEventIds)
	{
		InFlightEventIds.Add(EventId);
	}

	bIsFlushing = true;

	const FPostHogBatchPayload Payload(ApiKey, BatchEvents);
	ActiveRequestHandle = Transport.SendBatch(Payload, [this, BatchEventIds](bool bSuccess, int32, const FString&)
	{
		bIsFlushing = false;
		ActiveRequestHandle.Reset();

		for (const FString& EventId : BatchEventIds)
		{
			InFlightEventIds.Remove(EventId);
		}

		if (!bSuccess)
		{
			// TODO: Handle failed flush. Could have received a response saying the payload was too large.
			return;
		}

		for (const FString& EventId : BatchEventIds)
		{
			StorageProvider.DeleteEvent(EventId);
		}
	});
}

void FPostHogEventQueue::CancelInFlightRequest()
{
	if (!ActiveRequestHandle.IsValid())
	{
		return;
	}

	ActiveRequestHandle->Cancel();
	ActiveRequestHandle.Reset();

	InFlightEventIds.Reset();
	bIsFlushing = false;
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
