// Trevor Eckhoff, 2026. All rights reserved.


#include "Events/PostHogEventQueue.h"

#include "Events/PostHogBatchPayload.h"
#include "Events/PostHogEvent.h"
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

bool FPostHogEventQueue::Enqueue(const FPostHogEvent& Event)
{	
	if (EventsQueue.Num() >= MaxQueueSize)
	{
		const int32 EventIndexToDrop = bIsFlushing ? InFlightEventIds.Num() : 0;
		if (!EventsQueue.IsValidIndex(EventIndexToDrop))
		{
			return false;
		}
		
		const FPostHogEvent& EventToDrop = EventsQueue[EventIndexToDrop];
		if (!StorageProvider.DeleteEvent(EventToDrop.GetEventId()))
		{
			return false;
		}
		
		EventsQueue.RemoveAt(EventIndexToDrop);
	}
	
	if (!StorageProvider.SaveEvent(Event.GetEventId(), Event.ToJsonObject()))
	{
		return false;
	}
	
	EventsQueue.Add(Event);
	
	if (EventsQueue.Num() >= FlushEventCount)
	{
		Flush();
	}
	
	return true;
}

void FPostHogEventQueue::Flush()
{
	if (bIsFlushing || EventsQueue.Num() == 0)
	{
		return;
	}
	
	const int32 BatchSize = FMath::Min(MaxBatchSize, EventsQueue.Num());
	
	TArray<FPostHogEvent> BatchEvents;
	BatchEvents.Reserve(BatchSize);
	
	TArray<FString> BatchEventIds;
	BatchEventIds.Reserve(BatchSize);
	
	for (int32 EventIndex = 0; EventIndex < BatchSize; ++EventIndex)
	{
		const FPostHogEvent& Event = EventsQueue[EventIndex];
		const FString EventId = Event.GetEventId();
		
		BatchEvents.Add(Event);
		BatchEventIds.Add(EventId);
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
		
		EventsQueue.RemoveAll([&BatchEventIds](const FPostHogEvent& Event)
		{
			return BatchEventIds.Contains(Event.GetEventId());
		});
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
	EventsQueue.Reset();
}

int32 FPostHogEventQueue::Num() const
{
	return EventsQueue.Num();
}
