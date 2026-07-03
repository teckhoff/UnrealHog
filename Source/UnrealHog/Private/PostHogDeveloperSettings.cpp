// Trevor Eckhoff, 2026. All rights reserved.


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
	
	if (InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UPostHogDeveloperSettings, Host))
	{
		return bCanEdit && HostType == EPostHogHost::Custom;
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