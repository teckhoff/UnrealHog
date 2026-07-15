
#include "Events/PostHogBatchPayload.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"


FPostHogBatchPayload::FPostHogBatchPayload(const FString& InApiKey)
	: ApiKey(InApiKey)
	, SentAt(FDateTime::UtcNow().ToIso8601())
{
}

FPostHogBatchPayload::FPostHogBatchPayload(const FString& InApiKey, const TArray<FPostHogEvent>& InEvents)
	: ApiKey(InApiKey)
	, SentAt(FDateTime::UtcNow().ToIso8601())
	, Events(InEvents)
{
}

void FPostHogBatchPayload::AddEvent(const FPostHogEvent& Event)
{
	Events.Add(Event);
}

int32 FPostHogBatchPayload::Num() const
{
	return Events.Num();
}

TSharedRef<FJsonObject> FPostHogBatchPayload::ToJsonObject() const
{
	TArray<TSharedPtr<FJsonValue>> Batch;
	Batch.Reserve(Events.Num());
	
	for (const FPostHogEvent& Event : Events)
	{
		Batch.Add(MakeShared<FJsonValueObject>(Event.ToJsonObject()));
	}
	
	const TSharedRef<FJsonObject> BatchPayload = MakeShared<FJsonObject>();
	BatchPayload->SetStringField(TEXT("api_key"), ApiKey);
	BatchPayload->SetStringField(TEXT("sent_at"), SentAt);
	BatchPayload->SetArrayField(TEXT("batch"), Batch);
	
	return BatchPayload;
}
