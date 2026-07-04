// Trevor Eckhoff, 2026. All rights reserved.


#include "Events/PostHogEvent.h"

#include "Dom/JsonObject.h"
#include "SDK/PostHogSdkInfo.h"


FPostHogEvent::FPostHogEvent(const FString& InEventName, const FString& InDistinctId)
	: EventUuid(FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower))
	, EventName(InEventName)
	, DistinctId(InDistinctId)
	, Timestamp(FDateTime::UtcNow().ToIso8601())
{	
	Properties.SetStringField(TEXT("$lib"), PostHogSdkInfo::GetLibraryName());
	Properties.SetStringField(TEXT("$lib_version"), PostHogSdkInfo::GetPluginVersion());
}

void FPostHogEvent::SetStringProperty(const FString& Key, const FString& StringValue)
{	
	Properties.SetStringField(Key, StringValue);
}

void FPostHogEvent::SetBoolProperty(const FString& Key, bool bValue)
{	
	Properties.SetBoolField(Key, bValue);
}

void FPostHogEvent::SetNumberProperty(const FString& Key, double NumberValue)
{
	Properties.SetNumberField(Key, NumberValue);
}

void FPostHogEvent::SetObjectProperty(const FString& Key, FJsonObject& ObjectValue)
{
	Properties.SetObjectField(Key, MakeShared<FJsonObject>(ObjectValue));
}

void FPostHogEvent::SetProcessPersonProfile(bool bProcessPersonProfile)
{
	Properties.SetBoolField(TEXT("$process_person_profile"), bProcessPersonProfile);
}

TSharedRef<FJsonObject> FPostHogEvent::ToJsonObject() const
{
	const TSharedRef<FJsonObject> EventJsonObject = MakeShared<FJsonObject>();
	
	EventJsonObject->SetStringField(TEXT("uuid"), EventUuid);
	EventJsonObject->SetStringField(TEXT("event"), EventName);
	EventJsonObject->SetStringField(TEXT("distinct_id"), DistinctId);
	EventJsonObject->SetStringField(TEXT("timestamp"), Timestamp);
	EventJsonObject->SetObjectField(TEXT("properties"), MakeShared<FJsonObject>(Properties));
	
	return EventJsonObject;
}
