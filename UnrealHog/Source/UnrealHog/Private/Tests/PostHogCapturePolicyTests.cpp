#include "Events/PostHogCapturePolicy.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Events/PostHogEvent.h"
#include "Events/PostHogEventProperties.h"
#include "Dom/JsonObject.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogCapturePolicyEventNameTest, "UnrealHog.Events.CapturePolicy.IsValidEventNameRejectsEmptyAndWhitespace", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogCapturePolicyEventNameTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("Empty name is invalid"), PostHogCapturePolicy::IsValidEventName(TEXT("")));
	TestFalse(TEXT("Whitespace-only name is invalid"), PostHogCapturePolicy::IsValidEventName(TEXT("   ")));
	TestFalse(TEXT("Tab/newline-only name is invalid"), PostHogCapturePolicy::IsValidEventName(TEXT("\t\n")));
	TestTrue(TEXT("Non-empty name is valid"), PostHogCapturePolicy::IsValidEventName(TEXT("valid_event")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogCapturePolicyReservedKeysTest, "UnrealHog.Events.CapturePolicy.ReservedPropertyKeysContainsSdkOwnedFields", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogCapturePolicyReservedKeysTest::RunTest(const FString& Parameters)
{
	const TSet<FString>& ReservedKeys = PostHogCapturePolicy::GetReservedPropertyKeys();

	TestTrue(TEXT("Contains $lib"), ReservedKeys.Contains(TEXT("$lib")));
	TestTrue(TEXT("Contains $lib_version"), ReservedKeys.Contains(TEXT("$lib_version")));
	TestTrue(TEXT("Contains $process_person_profile"), ReservedKeys.Contains(TEXT("$process_person_profile")));
	TestTrue(TEXT("Contains $session_id"), ReservedKeys.Contains(TEXT("$session_id")));
	TestTrue(TEXT("Contains $groups"), ReservedKeys.Contains(TEXT("$groups")));
	TestTrue(TEXT("Contains $os"), ReservedKeys.Contains(TEXT("$os")));
	TestTrue(TEXT("Contains $device_type"), ReservedKeys.Contains(TEXT("$device_type")));
	TestTrue(TEXT("Contains $device_manufacturer"), ReservedKeys.Contains(TEXT("$device_manufacturer")));
	TestTrue(TEXT("Contains $app_build"), ReservedKeys.Contains(TEXT("$app_build")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogCapturePolicyEnrichmentKeysNotOverridableTest, "UnrealHog.Events.CapturePolicy.EnrichmentKeysNotOverridableByCaller", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogCapturePolicyEnrichmentKeysNotOverridableTest::RunTest(const FString& Parameters)
{
	// Apply caller-supplied values for the four new reserved keys with no prior SDK enrichment.
	// If ApplyToEvent stripped them as required, none of these keys will exist on the event at all.
	FPostHogEvent Event(TEXT("test-event"), TEXT("distinct-1"));

	UPostHogEventProperties* Properties = NewObject<UPostHogEventProperties>();
	Properties->AddString(TEXT("$os"), TEXT("caller-supplied-os"));
	Properties->AddString(TEXT("$device_type"), TEXT("caller-supplied-device-type"));
	Properties->AddString(TEXT("$device_manufacturer"), TEXT("caller-supplied-manufacturer"));
	Properties->AddString(TEXT("$app_build"), TEXT("caller-supplied-build"));
	Properties->ApplyToEvent(Event);

	const TSharedRef<FJsonObject> JsonObject = Event.ToJsonObject();
	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("JSON has properties object"), JsonObject->TryGetObjectField(TEXT("properties"), PropertiesObject));

	TestFalse(TEXT("Caller-supplied $os is stripped"), (*PropertiesObject)->HasField(TEXT("$os")));
	TestFalse(TEXT("Caller-supplied $device_type is stripped"), (*PropertiesObject)->HasField(TEXT("$device_type")));
	TestFalse(TEXT("Caller-supplied $device_manufacturer is stripped"), (*PropertiesObject)->HasField(TEXT("$device_manufacturer")));
	TestFalse(TEXT("Caller-supplied $app_build is stripped"), (*PropertiesObject)->HasField(TEXT("$app_build")));

	// Now apply real SDK enrichment on top and confirm the SDK-set values (whatever this platform
	// provides) are never equal to the caller's attempted override string.
	Event.ApplySdkProperties(false);

	const TSharedRef<FJsonObject> EnrichedJsonObject = Event.ToJsonObject();
	const TSharedPtr<FJsonObject>* EnrichedPropertiesObject = nullptr;
	TestTrue(TEXT("Enriched JSON has properties object"), EnrichedJsonObject->TryGetObjectField(TEXT("properties"), EnrichedPropertiesObject));

	FString OsValue;
	if ((*EnrichedPropertiesObject)->TryGetStringField(TEXT("$os"), OsValue))
	{
		TestNotEqual(TEXT("$os is not the caller-supplied value"), OsValue, TEXT("caller-supplied-os"));
	}

	FString DeviceTypeValue;
	if ((*EnrichedPropertiesObject)->TryGetStringField(TEXT("$device_type"), DeviceTypeValue))
	{
		TestNotEqual(TEXT("$device_type is not the caller-supplied value"), DeviceTypeValue, TEXT("caller-supplied-device-type"));
	}

	FString DeviceManufacturerValue;
	if ((*EnrichedPropertiesObject)->TryGetStringField(TEXT("$device_manufacturer"), DeviceManufacturerValue))
	{
		TestNotEqual(TEXT("$device_manufacturer is not the caller-supplied value"), DeviceManufacturerValue, TEXT("caller-supplied-manufacturer"));
	}

	FString AppBuildValue;
	if ((*EnrichedPropertiesObject)->TryGetStringField(TEXT("$app_build"), AppBuildValue))
	{
		TestNotEqual(TEXT("$app_build is not the caller-supplied value"), AppBuildValue, TEXT("caller-supplied-build"));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
