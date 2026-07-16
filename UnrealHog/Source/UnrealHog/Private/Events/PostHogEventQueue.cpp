
#include "Events/PostHogEventQueue.h"

#include "Dom/JsonObject.h"
#include "Events/PostHogBatchPayload.h"
#include "Events/PostHogEvent.h"
#include "Logging/PostHogLogger.h"
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

bool FPostHogEventQueue::Enqueue(const FPostHogEvent& Event)
{
	if (StorageProvider.GetEventCount() >= MaxQueueSize)
	{
		const TArray<FString> EventIds = StorageProvider.GetEventIds();
		const int32 EventIndexToDrop = bIsFlushing ? InFlightEventIds.Num() : 0;
		if (!EventIds.IsValidIndex(EventIndexToDrop))
		{
			return false;
		}

		if (!StorageProvider.DeleteEvent(EventIds[EventIndexToDrop]))
		{
			return false;
		}
	}

	if (!StorageProvider.SaveEvent(Event.GetEventId(), Event.ToJsonObject()))
	{
		return false;
	}

	if (StorageProvider.GetEventCount() >= FlushEventCount)
	{
		Flush();
	}

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

	const int32 BatchSize = FMath::Min(MaxBatchSize, EventIds.Num());

	TArray<FPostHogEvent> BatchEvents;
	BatchEvents.Reserve(BatchSize);

	TArray<FString> BatchEventIds;
	BatchEventIds.Reserve(BatchSize);

	for (int32 EventIndex = 0; EventIndex < BatchSize; ++EventIndex)
	{
		const FString& EventId = EventIds[EventIndex];

		FString EventJson;
		if (!StorageProvider.LoadEvent(EventId, EventJson))
		{
			continue;
		}

		TSharedPtr<FJsonObject> EventJsonObject;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(EventJson);
		if (!FJsonSerializer::Deserialize(Reader, EventJsonObject) || !EventJsonObject.IsValid())
		{
			continue;
		}

		FString ParseError;
		TOptional<FPostHogEvent> ParsedEvent = FPostHogEvent::TryParseFromJson(EventJsonObject.ToSharedRef(), ParseError);
		if (!ParsedEvent.IsSet())
		{
			UE_LOG(LogPostHog, Warning, TEXT("Skipping unparseable persisted PostHog event %s: %s"), *EventId, *ParseError);
			continue;
		}

		BatchEvents.Add(ParsedEvent.GetValue());
		BatchEventIds.Add(EventId);
		InFlightEventIds.Add(EventId);
	}

	if (BatchEvents.Num() == 0)
	{
		return;
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
