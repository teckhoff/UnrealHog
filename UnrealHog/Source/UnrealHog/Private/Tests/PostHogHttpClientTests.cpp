#include "Http/PostHogHttpClient.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Events/PostHogBatchPayload.h"
#include "SDK/PostHogSdkInfo.h"
#include "Tests/PostHogFakeHttpRequest.h"

namespace
{
	struct FCapturedCompletion
	{
		int32 CallCount = 0;
		bool bLastSuccess = false;
		int32 LastStatusCode = -1;
		FString LastBody;
	};

	IPostHogBatchTransport::FOnSendComplete MakeCapturingCallback(FCapturedCompletion& Captured)
	{
		return [&Captured](bool bSuccess, int32 StatusCode, const FString& Body)
		{
			++Captured.CallCount;
			Captured.bLastSuccess = bSuccess;
			Captured.LastStatusCode = StatusCode;
			Captured.LastBody = Body;
		};
	}

	FPostHogHttpClient MakeClientUnderTest(const FString& Host, FPostHogFakeHttpRequestFactory& Factory)
	{
		return FPostHogHttpClient(Host, [&Factory]() { return Factory(); });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogHttpClientSuccessfulSendUsesCanonicalRequestTest, "UnrealHog.Http.PostHogHttpClient.SuccessfulSendUsesCanonicalRequest", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogHttpClientSuccessfulSendUsesCanonicalRequestTest::RunTest(const FString& Parameters)
{
	FPostHogFakeHttpRequestFactory Factory;
	FPostHogHttpClient Client = MakeClientUnderTest(TEXT("https://us.i.posthog.com"), Factory);

	FCapturedCompletion Captured;
	const FPostHogBatchPayload Payload(TEXT("test-api-key"));
	const TSharedPtr<IPostHogBatchRequestHandle> Handle = Client.SendBatch(Payload, MakeCapturingCallback(Captured));

	TestTrue(TEXT("Successful start returns a handle"), Handle.IsValid());
	TestEqual(TEXT("Exactly one fake request created"), Factory.CreatedRequests.Num(), 1);

	const FPostHogFakeHttpRequest& Request = Factory.Last();
	TestEqual(TEXT("Verb is POST"), Request.Verb, FString(TEXT("POST")));
	TestEqual(TEXT("URL targets <host>/batch"), Request.Url, FString(TEXT("https://us.i.posthog.com/batch")));
	TestEqual(TEXT("Content-Type is application/json"), Request.GetHeader(TEXT("Content-Type")), FString(TEXT("application/json")));
	TestEqual(TEXT("Accept is application/json"), Request.GetHeader(TEXT("Accept")), FString(TEXT("application/json")));
	TestEqual(TEXT("User-Agent matches SDK info"), Request.GetHeader(TEXT("User-Agent")), FPostHogSdkInfo::GetUserAgent());
	TestTrue(TEXT("Timeout is set"), Request.TimeoutSecs.IsSet());
	TestEqual(TEXT("Timeout is 10 seconds"), Request.TimeoutSecs.GetValue(), 10.0f);
	TestTrue(TEXT("Body is non-empty JSON"), !Request.ContentString.IsEmpty());
	TestTrue(TEXT("ProcessRequest was invoked"), Request.bProcessRequestCalled);

	TestEqual(TEXT("Callback not yet invoked"), Captured.CallCount, 0);

	Factory.Last().SimulateComplete(true, 200, TEXT("{}"));

	TestEqual(TEXT("Callback invoked exactly once"), Captured.CallCount, 1);
	TestTrue(TEXT("Callback reports success"), Captured.bLastSuccess);
	TestEqual(TEXT("Callback reports status 200"), Captured.LastStatusCode, 200);
	TestEqual(TEXT("Callback reports response body"), Captured.LastBody, FString(TEXT("{}")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogHttpClientCancelSuppressesLateCompletionTest, "UnrealHog.Http.PostHogHttpClient.CancelSuppressesLateCompletion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogHttpClientCancelSuppressesLateCompletionTest::RunTest(const FString& Parameters)
{
	FPostHogFakeHttpRequestFactory Factory;
	FPostHogHttpClient Client = MakeClientUnderTest(TEXT("https://us.i.posthog.com"), Factory);

	FCapturedCompletion Captured;
	const FPostHogBatchPayload Payload(TEXT("test-api-key"));
	const TSharedPtr<IPostHogBatchRequestHandle> Handle = Client.SendBatch(Payload, MakeCapturingCallback(Captured));

	TestTrue(TEXT("Successful start returns a handle"), Handle.IsValid());

	Handle->Cancel();
	TestTrue(TEXT("Fake request observed cancellation"), Factory.Last().bCancelled);

	Factory.Last().SimulateComplete(true, 200, TEXT("{}"));

	TestEqual(TEXT("Callback not invoked after cancel"), Captured.CallCount, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogHttpClientForcedStartFailureCompletesExactlyOnceTest, "UnrealHog.Http.PostHogHttpClient.ForcedStartFailureCompletesExactlyOnce", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogHttpClientForcedStartFailureCompletesExactlyOnceTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("PostHog batch request failed to start"), EAutomationExpectedErrorFlags::Contains, 1);

	FPostHogFakeHttpRequestFactory Factory;
	Factory.bNextStartResult = false;

	FPostHogHttpClient Client = MakeClientUnderTest(TEXT("https://us.i.posthog.com"), Factory);

	FCapturedCompletion Captured;
	const FPostHogBatchPayload Payload(TEXT("test-api-key"));
	const TSharedPtr<IPostHogBatchRequestHandle> Handle = Client.SendBatch(Payload, MakeCapturingCallback(Captured));

	TestFalse(TEXT("Forced start failure returns a null handle"), Handle.IsValid());
	TestEqual(TEXT("Callback invoked exactly once on start failure"), Captured.CallCount, 1);
	TestFalse(TEXT("Callback reports failure"), Captured.bLastSuccess);
	TestEqual(TEXT("Callback reports status 0"), Captured.LastStatusCode, 0);
	TestEqual(TEXT("Callback reports empty body"), Captured.LastBody, FString(TEXT("")));

	// A late platform callback delivered after the synchronous start failure must not
	// double-complete the queue; the completion guard, not delegate unbinding, prevents this.
	Factory.Last().SimulateComplete(true, 200, TEXT("ok"));

	TestEqual(TEXT("Late platform callback is ignored"), Captured.CallCount, 1);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
