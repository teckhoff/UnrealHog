#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Events/PostHogEventContext.h"
#include "Misc/Optional.h"

class FJsonValue;

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
	// A UUIDv7 for this specific event.
	FString EventUuid;
	
	// The name of the event being processed.
	FString EventName;
	
	// The distinct ID of the user.
	FString DistinctId;
	
	// A timestamp in ISO 8601 format.
	FString Timestamp;
	
	// A JSON object containing the event properties. Always contains $lib, $lib_version, and the
	// other SDK-owned default properties populated by ApplySdkProperties.
	FJsonObject Properties;
	
public:
	FPostHogEvent(const FString& InEventName, const FString& InDistinctId);
	
	void SetStringProperty(const FString& Key, const FString& StringValue);
	void SetBoolProperty(const FString& Key, bool bValue);
	void SetNumberProperty(const FString& Key, double NumberValue);
	void SetObjectProperty(const FString& Key, FJsonObject& ObjectValue);
	void SetJsonValueProperty(const FString& Key, const TSharedRef<FJsonValue>& Value);

	// Populates the SDK-owned properties ($lib, $lib_version, platform/device/app info,
	// $process_person_profile) by capturing a fresh FPostHogEventContext. Not called by the
	// constructor so callers control composition order.
	void ApplySdkProperties(bool bProcessPersonProfile);

	// Same as above, but serializes from a caller-supplied Context instead of capturing one, so
	// tests can inject deterministic platform/app/screen values.
	void ApplySdkProperties(bool bProcessPersonProfile, const FPostHogEventContext& Context);

	FString GetEventId() const { return EventUuid; };

	TSharedRef<FJsonObject> ToJsonObject() const;

	// Rehydrates a persisted event verbatim from stored JSON. Requires correctly typed uuid, event,
	// distinct_id, timestamp, and properties fields; returns unset with OutErrorMessage populated on
	// the first missing/mistyped field rather than constructing a partial event. Never enriches,
	// generates identifiers, or invokes before-send.
	static TOptional<FPostHogEvent> TryParseFromJson(const TSharedRef<FJsonObject>& JsonObject, FString& OutErrorMessage);

private:
	// This ctor exists only for rehydrating persisted records verbatim; it must never be reached
	// from CaptureEvent/enrichment paths.
	FPostHogEvent(const FString& InEventUuid, const FString& InEventName, const FString& InDistinctId, const FString& InTimestamp, const TSharedRef<FJsonObject>& InProperties);
};