#include "Http/PostHogEndpointUrls.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEndpointUrlsBuildTest, "UnrealHog.Http.EndpointUrls.BuildCanonicalEndpointUrls", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEndpointUrlsBuildTest::RunTest(const FString& Parameters)
{
	struct FEndpointUrlCase
	{
		const TCHAR* InputHost;
		const TCHAR* ExpectedBatchUrl;
		const TCHAR* ExpectedFeatureFlagsUrl;
		const TCHAR* ExpectedSessionReplayUrl;
	};

	const FEndpointUrlCase Cases[] = {
		{
			TEXT("https://example.com"),
			TEXT("https://example.com/batch"),
			TEXT("https://example.com/flags/?v=2"),
			TEXT("https://example.com/s/")
		},
		{
			TEXT("https://example.com/"),
			TEXT("https://example.com/batch"),
			TEXT("https://example.com/flags/?v=2"),
			TEXT("https://example.com/s/")
		},
		{
			TEXT("https://example.com///"),
			TEXT("https://example.com/batch"),
			TEXT("https://example.com/flags/?v=2"),
			TEXT("https://example.com/s/")
		}
	};

	for (const FEndpointUrlCase& Case : Cases)
	{
		TestEqual(*FString::Printf(TEXT("%s batch URL"), Case.InputHost), PostHogEndpointUrls::BuildBatchUrl(Case.InputHost), Case.ExpectedBatchUrl);
		TestEqual(*FString::Printf(TEXT("%s feature flags URL"), Case.InputHost), PostHogEndpointUrls::BuildFeatureFlagsUrl(Case.InputHost), Case.ExpectedFeatureFlagsUrl);
		TestEqual(*FString::Printf(TEXT("%s session replay URL"), Case.InputHost), PostHogEndpointUrls::BuildSessionReplayUrl(Case.InputHost), Case.ExpectedSessionReplayUrl);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
