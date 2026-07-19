
#include "PostHogDeveloperSettings.h"

#include "UObject/UnrealType.h"


UPostHogDeveloperSettings::UPostHogDeveloperSettings(const FObjectInitializer& ObjectInitializer)
{
	CategoryName = "Analytics";
}

FString UPostHogDeveloperSettings::GetResolvedHost() const
{
	switch (HostType)
	{
		case EPostHogHost::US:
			return HostUS;
		case EPostHogHost::EU:
			return HostEU;
		default:
			return Host;
	}
}

#if WITH_EDITOR
bool UPostHogDeveloperSettings::CanEditChange(const FProperty* InProperty) const
{
	const bool bCanEdit = Super::CanEditChange(InProperty);

	if (!InProperty)
	{
		return bCanEdit;
	}

	const FName PropertyName = InProperty->GetFName();
	const UStruct* OwnerStruct = InProperty->GetOwnerStruct();
	
	if (OwnerStruct == UPostHogDeveloperSettings::StaticClass()
		&& PropertyName == GET_MEMBER_NAME_CHECKED(UPostHogDeveloperSettings, Host))
	{
		return bCanEdit && HostType == EPostHogHost::Custom;
	}

	if (OwnerStruct == UPostHogDeveloperSettings::StaticClass()
		&& (PropertyName == GET_MEMBER_NAME_CHECKED(UPostHogDeveloperSettings, bPreloadFeatureFlags)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UPostHogDeveloperSettings, FeatureFlagRequestMaxRetries)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UPostHogDeveloperSettings, bSendFeatureFlagEvent)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UPostHogDeveloperSettings, bSendDefaultPersonPropertiesForFlags)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UPostHogDeveloperSettings, bSessionReplay)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UPostHogDeveloperSettings, SessionReplayConfig)))
	{
		return false;
	}

	if (OwnerStruct == FPostHogSessionReplayConfig::StaticStruct()
		&& (PropertyName == GET_MEMBER_NAME_CHECKED(FPostHogSessionReplayConfig, ThrottleDelaySeconds)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(FPostHogSessionReplayConfig, ScreenshotQuality)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(FPostHogSessionReplayConfig, bCaptureNetworkTelemetry)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(FPostHogSessionReplayConfig, bCaptureLogs)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(FPostHogSessionReplayConfig, MinLogLevel)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(FPostHogSessionReplayConfig, ScreenshotScale)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(FPostHogSessionReplayConfig, FlushEventCount)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(FPostHogSessionReplayConfig, FlushIntervalSeconds)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(FPostHogSessionReplayConfig, MaxQueueSize)))
	{
		return false;
	}
	
	return bCanEdit;
}

void UPostHogDeveloperSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{	
	FName PropertyName = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;
	
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UPostHogDeveloperSettings, HostType))
	{
		switch (HostType)
		{
			case EPostHogHost::US:
				Host = HostUS;
				break;
			case EPostHogHost::EU:
				Host = HostEU;
				break;
			default:
				break;
		}
	}
	
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif
