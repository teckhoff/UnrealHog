// Trevor Eckhoff, 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/**
 * @brief Represents an event to be processed by PostHog analytics.
 *
 * This structure encapsulates all the necessary information for an event, including its name,
 * timestamp, user identifier, and associated properties.
 * 
 * The constructed event can be serialized into a JSON object for use with PostHog.
 *
 * TODO: Make this immutable after it is created.
 */
struct FPostHogEvent
{
private:
	// A UUID for this specific event.
	FString EventUuid;
	
	// The name of the event being processed.
	FString EventName;
	
	// The distinct ID of the user.
	FString DistinctId;
	
	// A timestamp in ISO 8601 format.
	FString Timestamp;
	
	// A JSON object containing the event properties. Always contains $lib and $lib_version.
	FJsonObject Properties;
	
public:
	FPostHogEvent(const FString& InEventName, const FString& InDistinctId);
	
	void SetStringProperty(const FString& Key, const FString& StringValue);
	void SetBoolProperty(const FString& Key, bool bValue);
	void SetNumberProperty(const FString& Key, double NumberValue);
	void SetObjectProperty(const FString& Key, FJsonObject& ObjectValue);
	void SetProcessPersonProfile(bool bProcessPersonProfile);
	
	FString GetEventId() const { return EventUuid; };
	
	TSharedRef<FJsonObject> ToJsonObject() const;
};