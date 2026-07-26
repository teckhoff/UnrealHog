#include "FeatureFlags/PostHogFeatureFlagHttpTransport.h"

#if WITH_DEV_AUTOMATION_TESTS

#include <atomic>

#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "FeatureFlags/PostHogFeatureFlagRequest.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HttpModule.h"
#include "Misc/AutomationTest.h"
#include "SDK/PostHogSdkInfo.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Tests/PostHogFakeFeatureFlagTransport.h"
#include "Tests/PostHogFakeHttpRequest.h"

namespace
{
	// Records fetch completions so tests can assert exactly-once delivery and result contents.
	struct FCapturedFetch
	{
		int32 CallCount = 0;
		FPostHogFeatureFlagFetchResult LastResult;
	};

	IPostHogFeatureFlagTransport::FOnFetchComplete MakeCapturingCallback(FCapturedFetch& Captured)
	{
		return [&Captured](const FPostHogFeatureFlagFetchResult& Result)
		{
			++Captured.CallCount;
			Captured.LastResult = Result;
		};
	}

	FPostHogFeatureFlagRequest MakeMinimalRequest()
	{
		FPostHogFeatureFlagRequest Request;
		Request.ApiKey = TEXT("phc_test_key");
		Request.DistinctId = TEXT("distinct-1");
		return Request;
	}

	TSharedPtr<FJsonObject> ParseJsonObject(const FString& Json)
	{
		TSharedPtr<FJsonObject> Object;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		FJsonSerializer::Deserialize(Reader, Object);
		return Object;
	}

	const TCHAR* const ValidResponseBody = TEXT("{\"featureFlags\":{\"my-flag\":true},\"requestId\":\"req-1\"}");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFeatureFlagRequestConstructionTest, "UnrealHog.FeatureFlags.Transport.RequestConstruction", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFeatureFlagRequestConstructionTest::RunTest(const FString& Parameters)
{
	FPostHogFakeHttpRequestFactory HttpFactory;
	FPostHogFeatureFlagHttpTransport Transport(TEXT("https://us.i.posthog.com/"), 0, [&HttpFactory]() { return HttpFactory(); });

	FCapturedFetch Captured;
	const TSharedPtr<IPostHogFeatureFlagFetchHandle> Handle = Transport.Fetch(MakeMinimalRequest(), MakeCapturingCallback(Captured));

	TestTrue(TEXT("A started fetch returns a handle"), Handle.IsValid());
	TestEqual(TEXT("Exactly one HTTP request created"), HttpFactory.CreatedRequests.Num(), 1);

	const FPostHogFakeHttpRequest& Request = HttpFactory.Last();
	TestEqual(TEXT("Verb is POST"), Request.Verb, FString(TEXT("POST")));
	TestEqual(TEXT("URL targets the canonical flags endpoint"), Request.Url, FString(TEXT("https://us.i.posthog.com/flags/?v=2")));
	TestEqual(TEXT("Content-Type is application/json"), Request.GetHeader(TEXT("Content-Type")), FString(TEXT("application/json")));
	TestEqual(TEXT("Accept is application/json"), Request.GetHeader(TEXT("Accept")), FString(TEXT("application/json")));
	TestEqual(TEXT("User-Agent matches SDK info"), Request.GetHeader(TEXT("User-Agent")), FPostHogSdkInfo::GetUserAgent());
	TestTrue(TEXT("Timeout is set"), Request.TimeoutSecs.IsSet());
	TestEqual(TEXT("Timeout is 10 seconds"), Request.TimeoutSecs.GetValue(), 10.0f);
	TestTrue(TEXT("ProcessRequest was invoked"), Request.bProcessRequestCalled);
	TestEqual(TEXT("No completion before the response arrives"), Captured.CallCount, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFeatureFlagRequestSerializationTest, "UnrealHog.FeatureFlags.Transport.RequestSerialization", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFeatureFlagRequestSerializationTest::RunTest(const FString& Parameters)
{
	// Minimal request: only the required fields are written.
	{
		FString Json;
		TestTrue(TEXT("Minimal request serializes"), MakeMinimalRequest().ToJsonString(Json));

		const TSharedPtr<FJsonObject> Object = ParseJsonObject(Json);
		TestTrue(TEXT("Minimal body parses"), Object.IsValid());
		TestEqual(TEXT("api_key is written"), Object->GetStringField(TEXT("api_key")), FString(TEXT("phc_test_key")));
		TestEqual(TEXT("distinct_id is written"), Object->GetStringField(TEXT("distinct_id")), FString(TEXT("distinct-1")));
		TestFalse(TEXT("$anon_distinct_id omitted when unset"), Object->HasField(TEXT("$anon_distinct_id")));
		TestFalse(TEXT("$groups omitted when empty"), Object->HasField(TEXT("$groups")));
		TestFalse(TEXT("person_properties omitted when empty"), Object->HasField(TEXT("person_properties")));
		TestFalse(TEXT("group_properties omitted when empty"), Object->HasField(TEXT("group_properties")));
	}

	// Fully populated request: optional fields keep their reference names and nested JSON types.
	{
		FPostHogFeatureFlagRequest Request = MakeMinimalRequest();
		Request.AnonymousId = TEXT("anon-1");
		Request.Groups.Add(TEXT("company"), TEXT("acme"));

		const TSharedRef<FJsonObject> NestedObject = MakeShared<FJsonObject>();
		NestedObject->SetStringField(TEXT("tier"), TEXT("gold"));
		Request.PersonProperties.Add(TEXT("email"), MakeShared<FJsonValueString>(TEXT("a@b.com")));
		Request.PersonProperties.Add(TEXT("age"), MakeShared<FJsonValueNumber>(42));
		Request.PersonProperties.Add(TEXT("beta"), MakeShared<FJsonValueBoolean>(true));
		Request.PersonProperties.Add(TEXT("plan"), MakeShared<FJsonValueObject>(NestedObject));

		TMap<FString, TSharedPtr<FJsonValue>> CompanyProperties;
		CompanyProperties.Add(TEXT("seats"), MakeShared<FJsonValueNumber>(12));
		Request.GroupProperties.Add(TEXT("company"), CompanyProperties);

		FString Json;
		TestTrue(TEXT("Populated request serializes"), Request.ToJsonString(Json));

		const TSharedPtr<FJsonObject> Object = ParseJsonObject(Json);
		TestTrue(TEXT("Populated body parses"), Object.IsValid());
		TestEqual(TEXT("$anon_distinct_id uses the reference key"), Object->GetStringField(TEXT("$anon_distinct_id")), FString(TEXT("anon-1")));

		const TSharedPtr<FJsonObject>* GroupsObject = nullptr;
		TestTrue(TEXT("$groups is an object"), Object->TryGetObjectField(TEXT("$groups"), GroupsObject));
		TestEqual(TEXT("$groups carries the group key"), (*GroupsObject)->GetStringField(TEXT("company")), FString(TEXT("acme")));

		const TSharedPtr<FJsonObject>* PersonObject = nullptr;
		TestTrue(TEXT("person_properties is an object"), Object->TryGetObjectField(TEXT("person_properties"), PersonObject));
		TestEqual(TEXT("String person property survives"), (*PersonObject)->GetStringField(TEXT("email")), FString(TEXT("a@b.com")));
		TestEqual(TEXT("Number person property stays a number"), (*PersonObject)->GetNumberField(TEXT("age")), 42.0);
		TestTrue(TEXT("Bool person property stays a bool"), (*PersonObject)->GetBoolField(TEXT("beta")));
		const TSharedPtr<FJsonObject>* NestedPlan = nullptr;
		TestTrue(TEXT("Nested person property stays an object"), (*PersonObject)->TryGetObjectField(TEXT("plan"), NestedPlan));
		TestEqual(TEXT("Nested person property value survives"), (*NestedPlan)->GetStringField(TEXT("tier")), FString(TEXT("gold")));

		const TSharedPtr<FJsonObject>* GroupPropertiesObject = nullptr;
		TestTrue(TEXT("group_properties is an object"), Object->TryGetObjectField(TEXT("group_properties"), GroupPropertiesObject));
		const TSharedPtr<FJsonObject>* CompanyObject = nullptr;
		TestTrue(TEXT("group_properties nests per group type"), (*GroupPropertiesObject)->TryGetObjectField(TEXT("company"), CompanyObject));
		TestEqual(TEXT("Group property number survives"), (*CompanyObject)->GetNumberField(TEXT("seats")), 12.0);
	}

	// The HTTP path sends exactly that body.
	{
		FPostHogFakeHttpRequestFactory HttpFactory;
		FPostHogFeatureFlagHttpTransport Transport(TEXT("https://eu.i.posthog.com"), 0, [&HttpFactory]() { return HttpFactory(); });

		FCapturedFetch Captured;
		FPostHogFeatureFlagRequest Request = MakeMinimalRequest();
		Request.AnonymousId = TEXT("anon-1");
		Transport.Fetch(Request, MakeCapturingCallback(Captured));

		const TSharedPtr<FJsonObject> Body = ParseJsonObject(HttpFactory.Last().ContentString);
		TestTrue(TEXT("Sent body parses"), Body.IsValid());
		TestEqual(TEXT("Sent body carries the distinct id"), Body->GetStringField(TEXT("distinct_id")), FString(TEXT("distinct-1")));
		TestEqual(TEXT("Sent body carries the anonymous id"), Body->GetStringField(TEXT("$anon_distinct_id")), FString(TEXT("anon-1")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFeatureFlagSuccessfulFetchParsesResponseTest, "UnrealHog.FeatureFlags.Transport.SuccessfulFetchParsesResponse", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFeatureFlagSuccessfulFetchParsesResponseTest::RunTest(const FString& Parameters)
{
	FPostHogFakeHttpRequestFactory HttpFactory;
	FPostHogFeatureFlagHttpTransport Transport(TEXT("https://us.i.posthog.com"), 2, [&HttpFactory]() { return HttpFactory(); });

	FCapturedFetch Captured;
	Transport.Fetch(MakeMinimalRequest(), MakeCapturingCallback(Captured));
	HttpFactory.Last().SimulateComplete(true, 200, ValidResponseBody);

	TestEqual(TEXT("Completed exactly once"), Captured.CallCount, 1);
	TestTrue(TEXT("Fetch succeeded"), Captured.LastResult.bSucceeded);
	TestEqual(TEXT("Status code reported"), Captured.LastResult.StatusCode, 200);
	TestEqual(TEXT("Single attempt on success"), Captured.LastResult.AttemptCount, 1);
	TestTrue(TEXT("Response parsed"), Captured.LastResult.Response.IsSet());
	TestTrue(TEXT("Parsed response carries the flag"), Captured.LastResult.Response->ResolveValue(TEXT("my-flag")).IsBool());
	TestTrue(TEXT("Parsed response carries the request id"), Captured.LastResult.Response->RequestId.IsSet());
	TestEqual(TEXT("Only one HTTP request for a successful fetch"), HttpFactory.CreatedRequests.Num(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFeatureFlagMalformedResponseIsTerminalTest, "UnrealHog.FeatureFlags.Transport.MalformedResponseIsTerminal", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFeatureFlagMalformedResponseIsTerminalTest::RunTest(const FString& Parameters)
{
	FPostHogFakeFeatureFlagAttemptFactory AttemptFactory;
	FPostHogFakeRetryClock RetryClock;
	FPostHogFeatureFlagHttpTransport Transport(3, AttemptFactory.MakeFactory(), RetryClock.MakeScheduler());

	FCapturedFetch Captured;
	Transport.Fetch(MakeMinimalRequest(), MakeCapturingCallback(Captured));

	AddExpectedError(TEXT("PostHog feature flag response could not be parsed"), EAutomationExpectedErrorFlags::Contains, 1);
	AttemptFactory.Last().CompleteWithSuccess(200, TEXT("{not json"));

	TestEqual(TEXT("Completed exactly once"), Captured.CallCount, 1);
	TestFalse(TEXT("Malformed body fails the fetch"), Captured.LastResult.bSucceeded);
	TestEqual(TEXT("Malformed body is a data-processing failure"),
		static_cast<int32>(Captured.LastResult.FailureReason), static_cast<int32>(EPostHogFeatureFlagFailureReason::DataProcessing));
	TestEqual(TEXT("Malformed body is not retried"), AttemptFactory.Num(), 1);
	TestEqual(TEXT("No retry delay scheduled"), RetryClock.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFeatureFlagRetryBoundsAndDelaysTest, "UnrealHog.FeatureFlags.Transport.RetryBoundsAndDelays", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFeatureFlagRetryBoundsAndDelaysTest::RunTest(const FString& Parameters)
{
	// Zero configured retries sends exactly once.
	{
		FPostHogFakeFeatureFlagAttemptFactory AttemptFactory;
		FPostHogFakeRetryClock RetryClock;
		FPostHogFeatureFlagHttpTransport Transport(0, AttemptFactory.MakeFactory(), RetryClock.MakeScheduler());
		TestEqual(TEXT("Zero retries means one attempt allowed"), Transport.GetMaxAttempts(), 1);

		FCapturedFetch Captured;
		Transport.Fetch(MakeMinimalRequest(), MakeCapturingCallback(Captured));
		AttemptFactory.Last().CompleteWithFailure(EPostHogFeatureFlagFailureReason::Timeout);

		TestEqual(TEXT("Exactly one attempt"), AttemptFactory.Num(), 1);
		TestEqual(TEXT("No delay scheduled"), RetryClock.Num(), 0);
		TestEqual(TEXT("Completed exactly once"), Captured.CallCount, 1);
		TestEqual(TEXT("Attempt count reported"), Captured.LastResult.AttemptCount, 1);
	}

	// N retries send at most N + 1 attempts, with doubling delays from 300 ms.
	{
		FPostHogFakeFeatureFlagAttemptFactory AttemptFactory;
		FPostHogFakeRetryClock RetryClock;
		FPostHogFeatureFlagHttpTransport Transport(3, AttemptFactory.MakeFactory(), RetryClock.MakeScheduler());
		TestEqual(TEXT("Three retries means four attempts allowed"), Transport.GetMaxAttempts(), 4);

		FCapturedFetch Captured;
		Transport.Fetch(MakeMinimalRequest(), MakeCapturingCallback(Captured));

		for (int32 Attempt = 1; Attempt <= 4; ++Attempt)
		{
			TestEqual(FString::Printf(TEXT("Attempt %d started"), Attempt), AttemptFactory.Num(), Attempt);
			AttemptFactory.Last().CompleteWithFailure(EPostHogFeatureFlagFailureReason::Timeout);

			if (Attempt < 4)
			{
				TestEqual(FString::Printf(TEXT("Delay scheduled after failure %d"), Attempt), RetryClock.Num(), Attempt);
				TestTrue(FString::Printf(TEXT("Delay %d fires"), Attempt), RetryClock.FireNext());
			}
		}

		const TArray<float> Delays = RetryClock.GetDelays();
		TestEqual(TEXT("Exactly three delays scheduled"), Delays.Num(), 3);
		TestEqual(TEXT("First delay is 300 ms"), Delays[0], 0.3f);
		TestEqual(TEXT("Second delay is 600 ms"), Delays[1], 0.6f);
		TestEqual(TEXT("Third delay is 1200 ms"), Delays[2], 1.2f);

		TestEqual(TEXT("Attempts stop at N + 1"), AttemptFactory.Num(), 4);
		TestEqual(TEXT("Completed exactly once"), Captured.CallCount, 1);
		TestFalse(TEXT("Exhausted retries fail the fetch"), Captured.LastResult.bSucceeded);
		TestEqual(TEXT("Reported attempt count is N + 1"), Captured.LastResult.AttemptCount, 4);
	}

	// A retry that succeeds stops the sequence.
	{
		FPostHogFakeFeatureFlagAttemptFactory AttemptFactory;
		FPostHogFakeRetryClock RetryClock;
		FPostHogFeatureFlagHttpTransport Transport(2, AttemptFactory.MakeFactory(), RetryClock.MakeScheduler());

		FCapturedFetch Captured;
		Transport.Fetch(MakeMinimalRequest(), MakeCapturingCallback(Captured));
		AttemptFactory.Last().CompleteWithFailure(EPostHogFeatureFlagFailureReason::Protocol, 504);
		TestTrue(TEXT("Retry delay fires"), RetryClock.FireNext());
		AttemptFactory.Last().CompleteWithSuccess(200, ValidResponseBody);

		TestEqual(TEXT("Two attempts issued"), AttemptFactory.Num(), 2);
		TestEqual(TEXT("Completed exactly once"), Captured.CallCount, 1);
		TestTrue(TEXT("Retry succeeded"), Captured.LastResult.bSucceeded);
		TestEqual(TEXT("Attempt count reflects the retry"), Captured.LastResult.AttemptCount, 2);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFeatureFlagRetryClassificationTest, "UnrealHog.FeatureFlags.Transport.RetryClassification", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFeatureFlagRetryClassificationTest::RunTest(const FString& Parameters)
{
	struct FCase
	{
		const TCHAR* Description;
		EPostHogFeatureFlagFailureReason Reason;
		int32 StatusCode;
		bool bExpectRetry;
	};

	const TArray<FCase> Cases = {
		{TEXT("status-zero timeout"), EPostHogFeatureFlagFailureReason::Timeout, 0, true},
		{TEXT("status-zero dropped connection (reset/EOF/lost)"), EPostHogFeatureFlagFailureReason::ConnectionLost, 0, true},
		{TEXT("HTTP 502"), EPostHogFeatureFlagFailureReason::Protocol, 502, true},
		{TEXT("HTTP 504"), EPostHogFeatureFlagFailureReason::Protocol, 504, true},
		{TEXT("connection never established (refused/DNS/TLS)"), EPostHogFeatureFlagFailureReason::ConnectionFailed, 0, false},
		// The reference stops retrying a connection failure as soon as a real status arrived.
		{TEXT("dropped connection after HTTP 200"), EPostHogFeatureFlagFailureReason::ConnectionLost, 200, false},
		{TEXT("dropped connection after HTTP 502"), EPostHogFeatureFlagFailureReason::ConnectionLost, 502, false},
		{TEXT("timeout reported with a status"), EPostHogFeatureFlagFailureReason::Timeout, 200, false},
		{TEXT("HTTP 408"), EPostHogFeatureFlagFailureReason::Protocol, 408, false},
		{TEXT("HTTP 429"), EPostHogFeatureFlagFailureReason::Protocol, 429, false},
		{TEXT("HTTP 500"), EPostHogFeatureFlagFailureReason::Protocol, 500, false},
		{TEXT("HTTP 503"), EPostHogFeatureFlagFailureReason::Protocol, 503, false},
		{TEXT("HTTP 400"), EPostHogFeatureFlagFailureReason::Protocol, 400, false},
		{TEXT("data-processing failure"), EPostHogFeatureFlagFailureReason::DataProcessing, 0, false},
		{TEXT("cancellation"), EPostHogFeatureFlagFailureReason::Cancelled, 0, false},
		{TEXT("other failure"), EPostHogFeatureFlagFailureReason::Other, 0, false},
	};

	for (const FCase& Case : Cases)
	{
		TestEqual(FString::Printf(TEXT("Policy classifies %s"), Case.Description),
			PostHogFeatureFlagRetryPolicy::ShouldRetry(Case.Reason, Case.StatusCode), Case.bExpectRetry);

		// The transport must act on that classification: a retryable failure issues a second
		// attempt, a terminal one completes immediately.
		FPostHogFakeFeatureFlagAttemptFactory AttemptFactory;
		FPostHogFakeRetryClock RetryClock;
		FPostHogFeatureFlagHttpTransport Transport(1, AttemptFactory.MakeFactory(), RetryClock.MakeScheduler());

		FCapturedFetch Captured;
		Transport.Fetch(MakeMinimalRequest(), MakeCapturingCallback(Captured));
		AttemptFactory.Last().CompleteWithFailure(Case.Reason, Case.StatusCode);

		if (Case.bExpectRetry)
		{
			TestEqual(FString::Printf(TEXT("%s schedules a retry delay"), Case.Description), RetryClock.Num(), 1);
			TestEqual(FString::Printf(TEXT("%s does not complete before the retry"), Case.Description), Captured.CallCount, 0);
			RetryClock.FireNext();
			TestEqual(FString::Printf(TEXT("%s issues a second attempt"), Case.Description), AttemptFactory.Num(), 2);
		}
		else
		{
			TestEqual(FString::Printf(TEXT("%s issues no retry"), Case.Description), AttemptFactory.Num(), 1);
			TestEqual(FString::Printf(TEXT("%s schedules no delay"), Case.Description), RetryClock.Num(), 0);
			TestEqual(FString::Printf(TEXT("%s completes exactly once"), Case.Description), Captured.CallCount, 1);
			TestEqual(FString::Printf(TEXT("%s reports its status code"), Case.Description), Captured.LastResult.StatusCode, Case.StatusCode);
		}
	}

	// The HTTP path classifies a non-2xx response as a protocol failure carrying its status.
	{
		FPostHogFakeHttpRequestFactory HttpFactory;
		FPostHogFeatureFlagHttpTransport Transport(TEXT("https://us.i.posthog.com"), 0, [&HttpFactory]() { return HttpFactory(); });

		FCapturedFetch Captured;
		Transport.Fetch(MakeMinimalRequest(), MakeCapturingCallback(Captured));
		HttpFactory.Last().SimulateComplete(true, 401, TEXT("{}"));

		TestEqual(TEXT("Completed exactly once"), Captured.CallCount, 1);
		TestFalse(TEXT("Non-2xx fails the fetch"), Captured.LastResult.bSucceeded);
		TestEqual(TEXT("Protocol failure reported"),
			static_cast<int32>(Captured.LastResult.FailureReason), static_cast<int32>(EPostHogFeatureFlagFailureReason::Protocol));
		TestEqual(TEXT("Status code preserved"), Captured.LastResult.StatusCode, 401);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFeatureFlagHttpFailureClassificationTest, "UnrealHog.FeatureFlags.Transport.HttpFailureClassification", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFeatureFlagHttpFailureClassificationTest::RunTest(const FString& Parameters)
{
	// The mapping from what Unreal's platform backends actually report onto the reference's retry
	// classes. EHttpFailureReason alone is ambiguous, so the classifier also uses whether the server
	// demonstrably started answering and how long the attempt ran against the engine's connection
	// timeout. Crucially, "a response object exists" is not that signal: the Apple backend creates its
	// response before any network I/O, so the cases below drive the engine's real status/receive
	// callbacks and cover a pre-created but empty response.
	const float ConnectTimeout = FHttpModule::Get().GetHttpConnectionTimeout();

	struct FCase
	{
		const TCHAR* Description;
		EHttpFailureReason EngineReason;

		// Status of the partial response the engine handed back; unset means no response object at all.
		TOptional<int32> PartialResponseStatusCode;

		// Whether the peer actually sent something before the failure (status line or body bytes).
		bool bServerStartedResponding;

		// Whether request bytes reached the wire before the failure, which only happens once the
		// connection and any TLS handshake succeeded.
		bool bRequestBodySent;

		float ElapsedSeconds;
		EPostHogFeatureFlagFailureReason ExpectedReason;
		bool bExpectRetry;
	};

	const TArray<FCase> Cases = {
		{TEXT("SDK timeout"), EHttpFailureReason::TimedOut, TOptional<int32>(), false, true, 10.0f, EPostHogFeatureFlagFailureReason::Timeout, true},
		// ConnectionError also carries CURLE_COULDNT_CONNECT / RESOLVE / SSL_CONNECT_ERROR, which fail
		// within milliseconds, never reach the connection timeout, and never get a request byte out.
		{TEXT("connection refused, DNS, or TLS failure"), EHttpFailureReason::ConnectionError, TOptional<int32>(), false, false, 0.01f, EPostHogFeatureFlagFailureReason::ConnectionFailed, false},
		// CURLE_OPERATION_TIMEDOUT during connect: nothing arrived and the whole connection timeout
		// was consumed, which the reference retries as a timeout.
		{TEXT("curl connect timeout"), EHttpFailureReason::ConnectionError, TOptional<int32>(), false, false, ConnectTimeout, EPostHogFeatureFlagFailureReason::Timeout, true},
		{TEXT("curl connect timeout reported marginally early"), EHttpFailureReason::ConnectionError, TOptional<int32>(), false, false, ConnectTimeout - 0.1f, EPostHogFeatureFlagFailureReason::Timeout, true},
		// A stall or activity timeout after the server started answering is the reference's
		// connection-lost class, retryable while no real status arrived. The Apple backend clears its
		// response object for ConnectionError (FAppleHttpRequest::FinishRequest), so the received-bytes
		// observation is the only evidence here.
		{TEXT("activity timeout after the response started"), EHttpFailureReason::ConnectionError, TOptional<int32>(), true, true, 5.0f, EPostHogFeatureFlagFailureReason::ConnectionLost, true},
		// xcurl reports CURLE_SEND_ERROR as ConnectionError: the request was already going out, so the
		// connection existed and was lost, not refused.
		{TEXT("send error after the request went out"), EHttpFailureReason::ConnectionError, TOptional<int32>(), false, true, 1.0f, EPostHogFeatureFlagFailureReason::ConnectionLost, true},
		{TEXT("connection error after HTTP 200"), EHttpFailureReason::ConnectionError, TOptional<int32>(200), true, true, 5.0f, EPostHogFeatureFlagFailureReason::ConnectionLost, false},
		// CURLE_RECV_ERROR / CURLE_PARTIAL_FILE leave the backend's failure reason unmapped, so they
		// arrive as Other after the peer had already sent part of its response.
		{TEXT("CURLE_PARTIAL_FILE mid-response with no status line"), EHttpFailureReason::Other, TOptional<int32>(0), true, true, 1.0f, EPostHogFeatureFlagFailureReason::ConnectionLost, true},
		// The reference's retryable reset/EOF class also covers drops before the server said anything
		// at all: CURLE_RECV_ERROR before the status line and CURLE_GOT_NOTHING (the peer closed the
		// connection after accepting the request) both land in the backend's default branch with no
		// response object, no status, no header and no received byte. They are told apart from a failure
		// to connect solely by the request body having reached the wire, which curl reports as final
		// upload progress before the completion delegate runs (CurlHttp.cpp:1274).
		{TEXT("CURLE_RECV_ERROR reset before any status, header, or body byte"), EHttpFailureReason::Other, TOptional<int32>(), false, true, 1.0f, EPostHogFeatureFlagFailureReason::ConnectionLost, true},
		{TEXT("CURLE_GOT_NOTHING peer closed without answering"), EHttpFailureReason::None, TOptional<int32>(), false, true, 1.0f, EPostHogFeatureFlagFailureReason::ConnectionLost, true},
		// The reference refuses to retry a connection failure once a real status arrived, which is also
		// what keeps post-status HTTP/2 framing errors and local write errors terminal: curl records the
		// status code for them, so the `statusCode != 0` guard rejects the retry.
		{TEXT("connection dropped after HTTP 200"), EHttpFailureReason::Other, TOptional<int32>(200), true, true, 1.0f, EPostHogFeatureFlagFailureReason::ConnectionLost, false},
		{TEXT("connection dropped after HTTP 500"), EHttpFailureReason::Other, TOptional<int32>(500), true, true, 1.0f, EPostHogFeatureFlagFailureReason::ConnectionLost, false},
		{TEXT("CURLE_HTTP2_STREAM after the response started"), EHttpFailureReason::Other, TOptional<int32>(200), true, true, 2.0f, EPostHogFeatureFlagFailureReason::ConnectionLost, false},
		{TEXT("CURLE_WRITE_ERROR while storing the body"), EHttpFailureReason::Other, TOptional<int32>(200), true, true, 2.0f, EPostHogFeatureFlagFailureReason::ConnectionLost, false},
		// Everything else in the backend's default branch (certificate verification, HTTP/2 handshake
		// errors, local write errors) failed before the exchange began — nothing sent, nothing
		// answered — and stays terminal, whether or not the backend already created a response object.
		{TEXT("certificate verification failure"), EHttpFailureReason::Other, TOptional<int32>(), false, false, 0.02f, EPostHogFeatureFlagFailureReason::Other, false},
		{TEXT("CURLE_HTTP2 handshake failure before the request went out"), EHttpFailureReason::Other, TOptional<int32>(), false, false, 0.05f, EPostHogFeatureFlagFailureReason::Other, false},
		{TEXT("Apple TLS or protocol failure with a pre-created empty response"), EHttpFailureReason::Other, TOptional<int32>(0), false, false, 0.02f, EPostHogFeatureFlagFailureReason::Other, false},
		{TEXT("unreported failure before any response"), EHttpFailureReason::None, TOptional<int32>(), false, false, 0.02f, EPostHogFeatureFlagFailureReason::Other, false},
		{TEXT("engine cancellation"), EHttpFailureReason::Cancelled, TOptional<int32>(), false, true, 1.0f, EPostHogFeatureFlagFailureReason::Cancelled, false},
		{TEXT("response too large"), EHttpFailureReason::ResponseTooLarge, TOptional<int32>(200), true, true, 1.0f, EPostHogFeatureFlagFailureReason::DataProcessing, false},
	};

	for (const FCase& Case : Cases)
	{
		const int32 ExpectedStatusCode = Case.PartialResponseStatusCode.Get(0);

		FPostHogFeatureFlagHttpTransport::FHttpFailureContext Failure;
		Failure.EngineReason = Case.EngineReason;
		Failure.bServerStartedResponding = Case.bServerStartedResponding;
		Failure.bRequestBodySent = Case.bRequestBodySent;
		Failure.ElapsedSeconds = Case.ElapsedSeconds;
		Failure.ConnectTimeoutSeconds = ConnectTimeout;

		const EPostHogFeatureFlagFailureReason Classified = FPostHogFeatureFlagHttpTransport::ClassifyHttpFailure(Failure);
		TestEqual(FString::Printf(TEXT("%s maps to the expected reason"), Case.Description),
			static_cast<int32>(Classified), static_cast<int32>(Case.ExpectedReason));
		TestEqual(FString::Printf(TEXT("%s retry decision"), Case.Description),
			PostHogFeatureFlagRetryPolicy::ShouldRetry(Classified, ExpectedStatusCode), Case.bExpectRetry);

		// End to end through the live HTTP path: the same engine failure, delivered by a fake
		// request, drives the same retry behavior.
		FPostHogFakeHttpRequestFactory HttpFactory;
		FPostHogFeatureFlagHttpTransport Transport(TEXT("https://us.i.posthog.com"), 1,
			[&HttpFactory]() { return HttpFactory(); });

		FCapturedFetch Captured;
		Transport.Fetch(MakeMinimalRequest(), MakeCapturingCallback(Captured));

		if (Case.bRequestBodySent)
		{
			// What a backend reports once request bytes reach the wire; curl emits this final upload
			// progress from FinishRequest, before the completion delegate runs.
			HttpFactory.Last().SimulateSentBytes(128);
		}

		if (Case.bServerStartedResponding)
		{
			// Exactly what a backend reports once the peer answers, and the only thing the classifier
			// may treat as such.
			HttpFactory.Last().SimulateReceivedBytes(64);
		}

		HttpFactory.Last().SimulateFailure(Case.EngineReason, Case.PartialResponseStatusCode, Case.ElapsedSeconds);

		if (Case.bExpectRetry)
		{
			// A retryable engine failure keeps the fetch open until the real ticker delay elapses.
			TestEqual(FString::Printf(TEXT("%s does not complete before its retry"), Case.Description), Captured.CallCount, 0);
			Transport.CancelAll();
		}
		else
		{
			TestEqual(FString::Printf(TEXT("%s completes exactly once"), Case.Description), Captured.CallCount, 1);
			TestFalse(FString::Printf(TEXT("%s fails the fetch"), Case.Description), Captured.LastResult.bSucceeded);
			TestEqual(FString::Printf(TEXT("%s reports its reason"), Case.Description),
				static_cast<int32>(Captured.LastResult.FailureReason), static_cast<int32>(Case.ExpectedReason));
			TestEqual(FString::Printf(TEXT("%s preserves the response status"), Case.Description),
				Captured.LastResult.StatusCode, ExpectedStatusCode);
			TestEqual(FString::Printf(TEXT("%s issues no second request"), Case.Description), HttpFactory.CreatedRequests.Num(), 1);
		}
	}

	// A received status line is the other evidence a backend can report before any body byte, and it
	// makes an otherwise-unclassified drop the reference's retryable connection-lost class.
	{
		FPostHogFakeHttpRequestFactory HttpFactory;
		FPostHogFeatureFlagHttpTransport Transport(TEXT("https://us.i.posthog.com"), 1,
			[&HttpFactory]() { return HttpFactory(); });

		FCapturedFetch Captured;
		Transport.Fetch(MakeMinimalRequest(), MakeCapturingCallback(Captured));
		HttpFactory.Last().SimulateStatusCodeReceived(200);
		HttpFactory.Last().SimulateFailure(EHttpFailureReason::Other, TOptional<int32>(), 1.0f);

		TestEqual(TEXT("A drop after the status line retries"), Captured.CallCount, 0);
		Transport.CancelAll();
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFeatureFlagStartFailureIsTerminalTest, "UnrealHog.FeatureFlags.Transport.StartFailureIsTerminal", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFeatureFlagStartFailureIsTerminalTest::RunTest(const FString& Parameters)
{
	// A synchronous start failure completes the fetch once, without a retry.
	{
		FPostHogFakeFeatureFlagAttemptFactory AttemptFactory;
		AttemptFactory.bStartSynchronouslyFails = true;
		FPostHogFakeRetryClock RetryClock;
		FPostHogFeatureFlagHttpTransport Transport(2, AttemptFactory.MakeFactory(), RetryClock.MakeScheduler());

		FCapturedFetch Captured;
		const TSharedPtr<IPostHogFeatureFlagFetchHandle> Handle = Transport.Fetch(MakeMinimalRequest(), MakeCapturingCallback(Captured));

		TestEqual(TEXT("Completed exactly once"), Captured.CallCount, 1);
		TestFalse(TEXT("Start failure fails the fetch"), Captured.LastResult.bSucceeded);
		TestFalse(TEXT("A synchronously completed fetch returns no handle"), Handle.IsValid());
		TestEqual(TEXT("Only one attempt started"), AttemptFactory.StartCount, 1);
		TestEqual(TEXT("No retry delay scheduled"), RetryClock.Num(), 0);
	}

	// The HTTP path reports a request that never started, and a late platform callback afterwards
	// cannot complete the fetch a second time.
	{
		FPostHogFakeHttpRequestFactory HttpFactory;
		HttpFactory.bNextStartResult = false;
		FPostHogFeatureFlagHttpTransport Transport(TEXT("https://us.i.posthog.com"), 0, [&HttpFactory]() { return HttpFactory(); });

		AddExpectedError(TEXT("PostHog feature flag request failed to start"), EAutomationExpectedErrorFlags::Contains, 1);

		FCapturedFetch Captured;
		const TSharedPtr<IPostHogFeatureFlagFetchHandle> Handle = Transport.Fetch(MakeMinimalRequest(), MakeCapturingCallback(Captured));

		TestEqual(TEXT("Completed exactly once"), Captured.CallCount, 1);
		TestFalse(TEXT("Failed start fails the fetch"), Captured.LastResult.bSucceeded);
		TestFalse(TEXT("A request that never started returns no handle"), Handle.IsValid());

		HttpFactory.Last().SimulateComplete(true, 200, ValidResponseBody);
		TestEqual(TEXT("Late platform callback does not complete again"), Captured.CallCount, 1);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFeatureFlagCancellationSafetyTest, "UnrealHog.FeatureFlags.Transport.CancellationSafety", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFeatureFlagCancellationSafetyTest::RunTest(const FString& Parameters)
{
	// Cancelling the handle cancels the active attempt and suppresses a late callback.
	{
		FPostHogFakeFeatureFlagAttemptFactory AttemptFactory;
		FPostHogFakeRetryClock RetryClock;
		FPostHogFeatureFlagHttpTransport Transport(2, AttemptFactory.MakeFactory(), RetryClock.MakeScheduler());

		FCapturedFetch Captured;
		const TSharedPtr<IPostHogFeatureFlagFetchHandle> Handle = Transport.Fetch(MakeMinimalRequest(), MakeCapturingCallback(Captured));
		Handle->Cancel();

		TestTrue(TEXT("Active attempt was cancelled"), AttemptFactory.Last().WasCancelled());

		AttemptFactory.Last().ForceLateCompletion(FPostHogFeatureFlagAttemptOutcome::Success(200, ValidResponseBody));
		TestEqual(TEXT("Late callback is suppressed after cancellation"), Captured.CallCount, 0);

		Handle->Cancel();
		TestEqual(TEXT("Cancel is idempotent"), Captured.CallCount, 0);
	}

	// Cancelling during a pending retry delay cancels the delay and starts no further attempt.
	{
		FPostHogFakeFeatureFlagAttemptFactory AttemptFactory;
		FPostHogFakeRetryClock RetryClock;
		FPostHogFeatureFlagHttpTransport Transport(2, AttemptFactory.MakeFactory(), RetryClock.MakeScheduler());

		FCapturedFetch Captured;
		const TSharedPtr<IPostHogFeatureFlagFetchHandle> Handle = Transport.Fetch(MakeMinimalRequest(), MakeCapturingCallback(Captured));
		AttemptFactory.Last().CompleteWithFailure(EPostHogFeatureFlagFailureReason::ConnectionLost);
		TestEqual(TEXT("Retry delay pending"), RetryClock.Num(), 1);

		Handle->Cancel();
		TestEqual(TEXT("Pending delay was cancelled"), RetryClock.CancelCount, 1);
		TestFalse(TEXT("Cancelled delay does not fire"), RetryClock.FireNext());
		TestEqual(TEXT("No further attempt started"), AttemptFactory.Num(), 1);
		TestEqual(TEXT("No completion delivered"), Captured.CallCount, 0);
	}

	// CancelAll cancels every in-flight fetch.
	{
		FPostHogFakeFeatureFlagAttemptFactory AttemptFactory;
		FPostHogFakeRetryClock RetryClock;
		FPostHogFeatureFlagHttpTransport Transport(1, AttemptFactory.MakeFactory(), RetryClock.MakeScheduler());

		FCapturedFetch FirstCaptured;
		FCapturedFetch SecondCaptured;
		Transport.Fetch(MakeMinimalRequest(), MakeCapturingCallback(FirstCaptured));
		Transport.Fetch(MakeMinimalRequest(), MakeCapturingCallback(SecondCaptured));
		TestEqual(TEXT("Two attempts in flight"), AttemptFactory.Num(), 2);

		Transport.CancelAll();

		TestTrue(TEXT("First attempt cancelled"), AttemptFactory.Attempts[0]->WasCancelled());
		TestTrue(TEXT("Second attempt cancelled"), AttemptFactory.Attempts[1]->WasCancelled());

		AttemptFactory.Attempts[0]->ForceLateCompletion(FPostHogFeatureFlagAttemptOutcome::Success(200, ValidResponseBody));
		AttemptFactory.Attempts[1]->ForceLateCompletion(FPostHogFeatureFlagAttemptOutcome::Failure(EPostHogFeatureFlagFailureReason::Timeout));

		TestEqual(TEXT("First fetch stays silent"), FirstCaptured.CallCount, 0);
		TestEqual(TEXT("Second fetch stays silent"), SecondCaptured.CallCount, 0);
	}

	// Destroying the transport cancels in-flight fetches; a callback arriving afterwards is
	// suppressed instead of reaching the released owner.
	{
		FPostHogFakeFeatureFlagAttemptFactory AttemptFactory;
		FPostHogFakeRetryClock RetryClock;

		bool bOwnerAlive = true;
		int32 CallCount = 0;
		int32 CallsAfterOwnerReleased = 0;
		{
			FPostHogFeatureFlagHttpTransport Transport(1, AttemptFactory.MakeFactory(), RetryClock.MakeScheduler());
			Transport.Fetch(MakeMinimalRequest(), [&bOwnerAlive, &CallCount, &CallsAfterOwnerReleased](const FPostHogFeatureFlagFetchResult&)
				{
					++CallCount;
					if (!bOwnerAlive)
					{
						// Stands in for state the callback would touch after release; a real callback
						// here would be a use-after-free in production.
						++CallsAfterOwnerReleased;
					}
				});
		}
		bOwnerAlive = false;

		TestTrue(TEXT("Transport destruction cancelled the attempt"), AttemptFactory.Last().WasCancelled());

		AttemptFactory.Last().ForceLateCompletion(FPostHogFeatureFlagAttemptOutcome::Success(200, ValidResponseBody));
		TestEqual(TEXT("Callback after destruction is suppressed"), CallCount, 0);
		TestEqual(TEXT("No callback reached released state"), CallsAfterOwnerReleased, 0);
	}

	// A cancel racing a completion that has already been claimed must not return while that callback
	// is still running: the owner treats Cancel returning as permission to release the state the
	// callback touches.
	{
		FPostHogFakeFeatureFlagAttemptFactory AttemptFactory;
		FPostHogFakeRetryClock RetryClock;
		FPostHogFeatureFlagHttpTransport Transport(1, AttemptFactory.MakeFactory(), RetryClock.MakeScheduler());

		std::atomic<bool> bCallbackStarted{false};
		std::atomic<bool> bCallbackFinished{false};
		std::atomic<int32> CallCount{0};

		const TSharedPtr<IPostHogFeatureFlagFetchHandle> Handle = Transport.Fetch(MakeMinimalRequest(),
			[&bCallbackStarted, &bCallbackFinished, &CallCount](const FPostHogFeatureFlagFetchResult&)
			{
				++CallCount;
				bCallbackStarted = true;
				// Long enough that a Cancel which failed to wait would observably return first.
				FPlatformProcess::Sleep(0.2f);
				bCallbackFinished = true;
			});
		TestTrue(TEXT("Fetch is in flight"), Handle.IsValid());

		// Delivered from another thread, exactly as an HTTP completion would arrive.
		TFuture<void> Delivery = Async(EAsyncExecution::Thread, [&AttemptFactory]()
			{
				AttemptFactory.Last().CompleteWithSuccess(200, ValidResponseBody);
			});

		while (!bCallbackStarted)
		{
			FPlatformProcess::Sleep(0.001f);
		}

		Handle->Cancel();
		TestTrue(TEXT("Cancel did not return while a callback was still running"), bCallbackFinished.load());

		Delivery.Wait();
		TestEqual(TEXT("Completed exactly once despite the racing cancel"), CallCount.load(), 1);
	}

	// A cancel racing an attempt that is still being created must not return until that attempt has
	// been created, started, and cancelled. An owner treats Cancel returning as permission to release
	// its state, so no POST /flags may be issued for it afterwards.
	{
		FPostHogFakeRetryClock RetryClock;

		FEvent* const FactoryEntered = FPlatformProcess::GetSynchEventFromPool(true);
		FEvent* const ReleaseFactory = FPlatformProcess::GetSynchEventFromPool(true);

		FCriticalSection AttemptLock;
		TSharedPtr<FPostHogFakeFeatureFlagAttempt> CreatedAttempt;
		std::atomic<int32> AttemptsCreated{0};
		std::atomic<bool> bCancelReturned{false};
		std::atomic<int32> AttemptsCreatedAfterCancelReturned{0};

		FPostHogFeatureFlagHttpTransport Transport(1,
			[&](const FPostHogFeatureFlagRequest& Request, FPostHogFeatureFlagHttpTransport::FOnAttemptComplete OnComplete)
				-> TSharedPtr<IPostHogFeatureFlagAttempt>
			{
				++AttemptsCreated;
				if (bCancelReturned)
				{
					++AttemptsCreatedAfterCancelReturned;
				}

				const TSharedRef<FPostHogFakeFeatureFlagAttempt> Attempt = MakeShared<FPostHogFakeFeatureFlagAttempt>(Request, MoveTemp(OnComplete));
				{
					FScopeLock Lock(&AttemptLock);
					CreatedAttempt = Attempt;
				}

				// Holds the attempt half-started, exactly as a slow ProcessRequest would, so the
				// cancel below has to contend with attempt creation itself.
				FactoryEntered->Trigger();
				ReleaseFactory->Wait();
				return Attempt;
			},
			RetryClock.MakeScheduler());

		FCapturedFetch Captured;
		TFuture<void> Fetching = Async(EAsyncExecution::Thread, [&Transport, &Captured]()
			{
				Transport.Fetch(MakeMinimalRequest(), MakeCapturingCallback(Captured));
			});

		FactoryEntered->Wait();

		// Released from another thread so the cancel below genuinely overlaps the blocked factory.
		TFuture<void> Releasing = Async(EAsyncExecution::Thread, [ReleaseFactory]()
			{
				FPlatformProcess::Sleep(0.2f);
				ReleaseFactory->Trigger();
			});

		Transport.CancelAll();
		bCancelReturned = true;

		{
			FScopeLock Lock(&AttemptLock);
			TestTrue(TEXT("CancelAll waited for the attempt being created"), CreatedAttempt.IsValid());
			TestTrue(TEXT("CancelAll cancelled the attempt it waited for"), CreatedAttempt.IsValid() && CreatedAttempt->WasCancelled());
		}

		Fetching.Wait();
		Releasing.Wait();

		TestEqual(TEXT("Exactly one attempt was created"), AttemptsCreated.load(), 1);
		TestEqual(TEXT("No attempt started after CancelAll returned"), AttemptsCreatedAfterCancelReturned.load(), 0);

		{
			FScopeLock Lock(&AttemptLock);
			CreatedAttempt->ForceLateCompletion(FPostHogFeatureFlagAttemptOutcome::Success(200, ValidResponseBody));
		}
		TestEqual(TEXT("Late callback after the racing cancel is suppressed"), Captured.CallCount, 0);

		FPlatformProcess::ReturnSynchEventToPool(FactoryEntered);
		FPlatformProcess::ReturnSynchEventToPool(ReleaseFactory);
	}

	// A cancellation racing a retry timer that is already executing must not deadlock. Real timer
	// cancellation (FTSTicker::RemoveTicker) blocks until an executing ticker has returned, and that
	// ticker is inside the transport starting the next attempt, so cancellation must not hold the
	// delivery barrier while it waits for the timer.
	{
		FPostHogFakeFeatureFlagAttemptFactory AttemptFactory;

		TFunction<void()> Elapsed;
		std::atomic<bool> bElapsedRunning{false};
		std::atomic<bool> bCancelTimedOut{false};

		FEvent* const ElapsedEntered = FPlatformProcess::GetSynchEventFromPool(true);

		FPostHogFeatureFlagHttpTransport Transport(1, AttemptFactory.MakeFactory(),
			[&Elapsed, &bElapsedRunning, &bCancelTimedOut](float DelaySeconds, TFunction<void()> OnElapsed) -> TFunction<void()>
			{
				Elapsed = MoveTemp(OnElapsed);
				return [&bElapsedRunning, &bCancelTimedOut]()
				{
					// FTSTicker::RemoveTicker's waiting semantics, bounded so a regression fails this
					// test instead of hanging the whole automation run.
					const double Deadline = FPlatformTime::Seconds() + 5.0;
					while (bElapsedRunning.load())
					{
						if (FPlatformTime::Seconds() > Deadline)
						{
							bCancelTimedOut = true;
							return;
						}

						FPlatformProcess::Sleep(0.001f);
					}
				};
			});

		FCapturedFetch Captured;
		const TSharedPtr<IPostHogFeatureFlagFetchHandle> Handle = Transport.Fetch(MakeMinimalRequest(), MakeCapturingCallback(Captured));
		AttemptFactory.Last().CompleteWithFailure(EPostHogFeatureFlagFailureReason::ConnectionLost);
		TestTrue(TEXT("Retry delay scheduled"), static_cast<bool>(Elapsed));

		// Fires the timer as the ticker thread would, overlapping the cancellation below.
		TFuture<void> Firing = Async(EAsyncExecution::Thread, [&Elapsed, &bElapsedRunning, ElapsedEntered]()
			{
				bElapsedRunning = true;
				ElapsedEntered->Trigger();
				FPlatformProcess::Sleep(0.05f);
				Elapsed();
				bElapsedRunning = false;
			});

		ElapsedEntered->Wait();
		Handle->Cancel();

		Firing.Wait();
		TestFalse(TEXT("Cancelling a firing retry timer did not deadlock"), bCancelTimedOut.load());
		TestEqual(TEXT("No second attempt started"), AttemptFactory.Num(), 1);
		TestEqual(TEXT("No completion delivered"), Captured.CallCount, 0);

		FPlatformProcess::ReturnSynchEventToPool(ElapsedEntered);
	}

	// A handle outliving its transport is safe to cancel.
	{
		FPostHogFakeFeatureFlagAttemptFactory AttemptFactory;
		FPostHogFakeRetryClock RetryClock;

		TSharedPtr<IPostHogFeatureFlagFetchHandle> Handle;
		FCapturedFetch Captured;
		{
			FPostHogFeatureFlagHttpTransport Transport(1, AttemptFactory.MakeFactory(), RetryClock.MakeScheduler());
			Handle = Transport.Fetch(MakeMinimalRequest(), MakeCapturingCallback(Captured));
		}

		Handle->Cancel();
		TestEqual(TEXT("No completion delivered"), Captured.CallCount, 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFeatureFlagExactlyOnceCompletionTest, "UnrealHog.FeatureFlags.Transport.ExactlyOnceCompletion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFeatureFlagExactlyOnceCompletionTest::RunTest(const FString& Parameters)
{
	// Repeated attempt callbacks after completion deliver nothing further.
	{
		FPostHogFakeFeatureFlagAttemptFactory AttemptFactory;
		FPostHogFakeRetryClock RetryClock;
		FPostHogFeatureFlagHttpTransport Transport(2, AttemptFactory.MakeFactory(), RetryClock.MakeScheduler());

		FCapturedFetch Captured;
		const TSharedPtr<IPostHogFeatureFlagFetchHandle> Handle = Transport.Fetch(MakeMinimalRequest(), MakeCapturingCallback(Captured));

		AttemptFactory.Last().CompleteWithSuccess(200, ValidResponseBody);
		TestEqual(TEXT("Completed once"), Captured.CallCount, 1);

		AttemptFactory.Last().ForceLateCompletion(FPostHogFeatureFlagAttemptOutcome::Success(200, ValidResponseBody));
		AttemptFactory.Last().ForceLateCompletion(FPostHogFeatureFlagAttemptOutcome::Failure(EPostHogFeatureFlagFailureReason::Timeout));
		Handle->Cancel();
		Transport.CancelAll();

		TestEqual(TEXT("Still completed exactly once"), Captured.CallCount, 1);
	}

	// A stale callback from a superseded attempt cannot complete or extend the fetch.
	{
		FPostHogFakeFeatureFlagAttemptFactory AttemptFactory;
		FPostHogFakeRetryClock RetryClock;
		FPostHogFeatureFlagHttpTransport Transport(2, AttemptFactory.MakeFactory(), RetryClock.MakeScheduler());

		FCapturedFetch Captured;
		Transport.Fetch(MakeMinimalRequest(), MakeCapturingCallback(Captured));

		AttemptFactory.Attempts[0]->CompleteWithFailure(EPostHogFeatureFlagFailureReason::Timeout);
		TestTrue(TEXT("Retry delay fires"), RetryClock.FireNext());
		TestEqual(TEXT("Second attempt started"), AttemptFactory.Num(), 2);

		AttemptFactory.Attempts[0]->ForceLateCompletion(FPostHogFeatureFlagAttemptOutcome::Success(200, ValidResponseBody));
		TestEqual(TEXT("Stale attempt callback is ignored"), Captured.CallCount, 0);

		AttemptFactory.Attempts[1]->CompleteWithSuccess(200, ValidResponseBody);
		TestEqual(TEXT("Current attempt completes the fetch once"), Captured.CallCount, 1);
		TestTrue(TEXT("Fetch succeeded"), Captured.LastResult.bSucceeded);
		TestEqual(TEXT("Attempt count reflects both attempts"), Captured.LastResult.AttemptCount, 2);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
