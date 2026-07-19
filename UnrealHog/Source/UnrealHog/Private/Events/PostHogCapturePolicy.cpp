#include "Events/PostHogCapturePolicy.h"

#include "PostHogDeveloperSettings.h"

bool PostHogCapturePolicy::IsValidEventName(const FString& EventName)
{
	return !EventName.TrimStartAndEnd().IsEmpty();
}

bool PostHogCapturePolicy::ShouldProcessPersonProfile(EPostHogPersonProfiles Policy, bool bIsIdentified)
{
	switch (Policy)
	{
	case EPostHogPersonProfiles::Never:
		return false;
	case EPostHogPersonProfiles::IdentifiedOnly:
		return bIsIdentified;
	case EPostHogPersonProfiles::Always:
	default:
		return true;
	}
}

const TSet<FString>& PostHogCapturePolicy::GetReservedPropertyKeys()
{
	static const TSet<FString> ReservedKeys = {
		TEXT("$lib"),
		TEXT("$lib_version"),
		TEXT("$platform"),
		TEXT("$platform_variant"),
		TEXT("$os_version"),
		TEXT("$os"),
		TEXT("$device_model"),
		TEXT("$device_manufacturer"),
		TEXT("$device_type"),
		TEXT("$app_name"),
		TEXT("$app_version"),
		TEXT("$app_build"),
		TEXT("$screen_width"),
		TEXT("$screen_height"),
		TEXT("$screen_name"),
		TEXT("$process_person_profile"),
		TEXT("$session_id"),
		TEXT("$groups"),
	};

	return ReservedKeys;
}
