
#include "Events/PostHogEvent.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "SDK/PostHogSdkInfo.h"
#include "Utilities/PostHogUuidV7.h"


FPostHogEvent::FPostHogEvent(const FString& InEventName, const FString& InDistinctId)
	: EventUuid(PostHogUuidV7::New())
	, EventName(InEventName)
	, DistinctId(InDistinctId)
	, Timestamp(FDateTime::UtcNow().ToIso8601())
{
}

FPostHogEvent::FPostHogEvent(const FString& InEventUuid, const FString& InEventName, const FString& InDistinctId, const FString& InTimestamp, const TSharedRef<FJsonObject>& InProperties)
	: EventUuid(InEventUuid)
	, EventName(InEventName)
	, DistinctId(InDistinctId)
	, Timestamp(InTimestamp)
	, Properties(*InProperties)
{
}

TOptional<FPostHogEvent> FPostHogEvent::TryParseFromJson(const TSharedRef<FJsonObject>& JsonObject, FString& OutErrorMessage)
{
	FString ParsedUuid;
	if (!JsonObject->TryGetStringField(TEXT("uuid"), ParsedUuid))
	{
		OutErrorMessage = TEXT("Persisted event is missing a correctly typed \"uuid\" string field.");
		return TOptional<FPostHogEvent>();
	}

	FString ParsedEventName;
	if (!JsonObject->TryGetStringField(TEXT("event"), ParsedEventName))
	{
		OutErrorMessage = TEXT("Persisted event is missing a correctly typed \"event\" string field.");
		return TOptional<FPostHogEvent>();
	}

	FString ParsedDistinctId;
	if (!JsonObject->TryGetStringField(TEXT("distinct_id"), ParsedDistinctId))
	{
		OutErrorMessage = TEXT("Persisted event is missing a correctly typed \"distinct_id\" string field.");
		return TOptional<FPostHogEvent>();
	}

	FString ParsedTimestamp;
	if (!JsonObject->TryGetStringField(TEXT("timestamp"), ParsedTimestamp))
	{
		OutErrorMessage = TEXT("Persisted event is missing a correctly typed \"timestamp\" string field.");
		return TOptional<FPostHogEvent>();
	}

	const TSharedPtr<FJsonObject>* ParsedProperties = nullptr;
	if (!JsonObject->TryGetObjectField(TEXT("properties"), ParsedProperties) || !ParsedProperties->IsValid())
	{
		OutErrorMessage = TEXT("Persisted event is missing a correctly typed \"properties\" object field.");
		return TOptional<FPostHogEvent>();
	}

	return FPostHogEvent(ParsedUuid, ParsedEventName, ParsedDistinctId, ParsedTimestamp, (*ParsedProperties).ToSharedRef());
}

void FPostHogEvent::ApplySdkProperties(bool bProcessPersonProfile)
{
	ApplySdkProperties(bProcessPersonProfile, FPostHogEventContextProvider::Capture());
}

void FPostHogEvent::ApplySdkProperties(bool bProcessPersonProfile, const FPostHogEventContext& Context)
{
	Properties.SetStringField(TEXT("$lib"), FPostHogSdkInfo::GetLibraryName());
	Properties.SetStringField(TEXT("$lib_version"), FPostHogSdkInfo::GetPluginVersion());

	Properties.SetStringField(TEXT("$platform"), Context.PlatformName);

	if (!Context.PlatformVariant.IsEmpty())
	{
		Properties.SetStringField(TEXT("$platform_variant"), Context.PlatformVariant);
	}

	if (!Context.OsVersion.IsEmpty())
	{
		Properties.SetStringField(TEXT("$os_version"), Context.OsVersion);
	}

	const FString NormalizedOs = PostHogEventContextNormalization::NormalizeOsName(Context.OsLabel);

	if (!NormalizedOs.IsEmpty())
	{
		Properties.SetStringField(TEXT("$os"), NormalizedOs);
	}

	if (!Context.DeviceModel.IsEmpty())
	{
		Properties.SetStringField(TEXT("$device_model"), Context.DeviceModel);
	}

	if (!Context.DeviceManufacturer.IsEmpty())
	{
		Properties.SetStringField(TEXT("$device_manufacturer"), Context.DeviceManufacturer);
	}

	const FString DeviceType = PostHogEventContextNormalization::MapDeviceType(Context.DeviceFormFactor);

	if (!DeviceType.IsEmpty())
	{
		Properties.SetStringField(TEXT("$device_type"), DeviceType);
	}

	Properties.SetStringField(TEXT("$app_name"), Context.AppName);
	Properties.SetStringField(TEXT("$app_version"), Context.AppVersion);

	if (!Context.AppBuild.IsEmpty())
	{
		Properties.SetStringField(TEXT("$app_build"), Context.AppBuild);
	}

	if (Context.ScreenWidth.IsSet() && Context.ScreenHeight.IsSet())
	{
		Properties.SetNumberField(TEXT("$screen_width"), Context.ScreenWidth.GetValue());
		Properties.SetNumberField(TEXT("$screen_height"), Context.ScreenHeight.GetValue());
	}

	Properties.SetBoolField(TEXT("$process_person_profile"), bProcessPersonProfile);
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

void FPostHogEvent::SetJsonValueProperty(const FString& Key, const TSharedRef<FJsonValue>& Value)
{
	Properties.SetField(Key, Value);
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
