
#include "Events/PostHogEvent.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "SDK/PostHogSdkInfo.h"
#include "GeneralProjectSettings.h"
#include "Utilities/PostHogUuidV7.h"


FPostHogEvent::FPostHogEvent(const FString& InEventName, const FString& InDistinctId)
	: EventUuid(PostHogUuidV7::New())
	, EventName(InEventName)
	, DistinctId(InDistinctId)
	, Timestamp(FDateTime::UtcNow().ToIso8601())
{	
	Properties.SetStringField(TEXT("$lib"), FPostHogSdkInfo::GetLibraryName());
	Properties.SetStringField(TEXT("$lib_version"), FPostHogSdkInfo::GetPluginVersion());
	
	Properties.SetStringField(TEXT("$platform"), FPlatformProperties::PlatformName());
	
	const FString PlatformVariant = FPlatformProperties::PlatformVariantName();
	
	if (!PlatformVariant.IsEmpty())
	{
		Properties.SetStringField(TEXT("$platform_variant"), PlatformVariant);
	}
	
	const FString OsVersion = FPlatformMisc::GetOSVersion();
	
	if (!OsVersion.IsEmpty())
	{
		Properties.SetStringField(TEXT("$os_version"), OsVersion);
	}
	
	const FString DeviceMakeAndModel = FPlatformMisc::GetDeviceMakeAndModel();
	
	if (!DeviceMakeAndModel.IsEmpty())
	{
		Properties.SetStringField(TEXT("$device_model"), DeviceMakeAndModel);
	}
	
	const UGeneralProjectSettings* ProjectSettings = GetDefault<UGeneralProjectSettings>();
	
	Properties.SetStringField(TEXT("$app_name"), ProjectSettings->ProjectName);
	Properties.SetStringField(TEXT("$app_version"), ProjectSettings->ProjectVersion);
	
	if (GEngine && GEngine->GameViewport)
	{
		FVector2D ViewportSize;
		GEngine->GameViewport->GetViewportSize(ViewportSize);
		Properties.SetNumberField(TEXT("$screen_width"), ViewportSize.X);
		Properties.SetNumberField(TEXT("$screen_height"), ViewportSize.Y);
	}
	
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
