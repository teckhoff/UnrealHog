#include "Events/PostHogCapturePolicy.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

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

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
