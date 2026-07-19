#pragma once

#include "CoreMinimal.h"
#include "Events/PostHogEvent.h"

class FJsonObject;

/**
 * @brief Represents a PostHog batch payload containing one or more events.
 *
 * This is the wire payload sent to the PostHog /batch endpoint.
 */
struct FPostHogBatchPayload
{
private:
	// The public PostHog project API key.
	FString ApiKey;
	
	// A timestamp in ISO 8601 format for when this batch was created for sending.
	FString SentAt;
	
	// The events included in this batch.
	TArray<FPostHogEvent> Events;
	
public:
	explicit FPostHogBatchPayload(const FString& InApiKey);
	FPostHogBatchPayload(const FString& InApiKey, const TArray<FPostHogEvent>& InEvents);
	
	void AddEvent(const FPostHogEvent& Event);
	int32 Num() const;
	
	TSharedRef<FJsonObject> ToJsonObject() const;
};
