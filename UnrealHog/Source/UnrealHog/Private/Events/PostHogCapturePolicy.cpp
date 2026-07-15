#include "Events/PostHogCapturePolicy.h"

bool PostHogCapturePolicy::IsValidEventName(const FString& EventName)
{
	return !EventName.TrimStartAndEnd().IsEmpty();
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
		TEXT("$process_person_profile"),
		TEXT("$session_id"),
		TEXT("$groups"),
	};

	return ReservedKeys;
}
