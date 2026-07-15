#include "SDK/PostHogSdkInfo.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogSdkInfoUserAgentTest, "UnrealHog.SDK.SdkInfo.UserAgentUsesLibraryNameAndVersion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogSdkInfoUserAgentTest::RunTest(const FString& Parameters)
{
	const FString LibraryName = FPostHogSdkInfo::GetLibraryName();
	const FString PluginVersion = FPostHogSdkInfo::GetPluginVersion();
	const FString UserAgent = FPostHogSdkInfo::GetUserAgent();

	const FString Expected = FString::Printf(TEXT("%s/%s"), *LibraryName, *PluginVersion);
	TestEqual(TEXT("User-Agent is <library>/<version>"), UserAgent, Expected);

	// Matches UnrealHog/UnrealHog.uplugin at this baseline.
	TestEqual(TEXT("Library name matches plugin descriptor"), LibraryName, TEXT("UnrealHog"));
	TestEqual(TEXT("Plugin version matches plugin descriptor"), PluginVersion, TEXT("0.1.0"));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
