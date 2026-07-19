
#include "Http/PostHogHttpClient.h"

#include <atomic>

#include "Dom/JsonObject.h"
#include "Events/PostHogBatchPayload.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Logging/PostHogLogger.h"
#include "SDK/PostHogSdkInfo.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	// Single owner of a SendBatch completion callback, shared across every path that might
	// complete it (JSON-serialize failure, synchronous ProcessRequest() start failure, and the
	// engine's completion delegate) so a late platform callback after a synchronous failure
	// cannot notify the queue a second time.
	struct FCompletionState
	{
		std::atomic<bool> bCompleted{false};
		IPostHogBatchTransport::FOnSendComplete OnComplete;
	};

	void CompleteOnce(const TSharedRef<FCompletionState>& State, bool bSuccess, int32 StatusCode, const FString& ResponseBody)
	{
		bool bExpected = false;
		if (State->bCompleted.compare_exchange_strong(bExpected, true) && State->OnComplete)
		{
			State->OnComplete(bSuccess, StatusCode, ResponseBody);
		}
	}
}

FPostHogHttpClient::FPostHogHttpClient(const FString& InHost)
	: FPostHogHttpClient(InHost, []() { return FHttpModule::Get().CreateRequest(); })
{
}

FPostHogHttpClient::FPostHogHttpClient(const FString& InHost, FRequestFactory InRequestFactory)
	: Host(NormalizeHost(InHost))
	, RequestFactory(MoveTemp(InRequestFactory))
{
}

void FPostHogHttpClient::FRequestHandle::Cancel()
{
	Request->OnProcessRequestComplete().Unbind();
	Request->CancelRequest();
}

TSharedPtr<IPostHogBatchRequestHandle> FPostHogHttpClient::SendBatch(const FPostHogBatchPayload& Payload, FOnSendComplete OnComplete)
{
	const TSharedRef<FCompletionState> State = MakeShared<FCompletionState>();
	State->OnComplete = MoveTemp(OnComplete);

	FString JsonBody;
	if (!SerializeJsonObject(Payload.ToJsonObject(), JsonBody))
	{
		UE_LOG(LogUnrealHog, Warning, TEXT("Failed to serialize PostHog batch payload"));

		CompleteOnce(State, false, 0, TEXT(""));

		return nullptr;
	}

	FHttpRequestRef Request = RequestFactory();
	Request->SetVerb(TEXT("POST"));
	Request->SetURL(GetBatchUrl());
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Request->SetHeader(TEXT("User-Agent"), FPostHogSdkInfo::GetUserAgent());
	Request->SetContentAsString(JsonBody);
	Request->SetTimeout(TimeoutSeconds);

	const FString RequestUrl = Request->GetURL();
	UE_LOG(LogUnrealHog, Verbose, TEXT("Sending PostHog batch to %s"), *RequestUrl);

	Request->OnProcessRequestComplete().BindLambda(
		[State, RequestUrl](FHttpRequestPtr, FHttpResponsePtr Response, bool bRequestSucceeded) mutable
		{
			const bool bSuccess = FPostHogHttpClient::IsSuccessfulResponse(Response, bRequestSucceeded);
			const int32 StatusCode = Response.IsValid() ? Response->GetResponseCode() : 0;
			const FString ResponseBody = Response.IsValid() ? Response->GetContentAsString() : TEXT("");

			if (bSuccess)
			{
				UE_LOG(LogUnrealHog, Verbose, TEXT("PostHog batch sent successfully (status: %d)"), StatusCode);
			}
			else
			{
				UE_LOG(LogUnrealHog, Warning, TEXT("PostHog batch send failed for %s (status: %d)"), *RequestUrl, StatusCode);
			}

			CompleteOnce(State, bSuccess, StatusCode, ResponseBody);
		});

	const bool bStarted = Request->ProcessRequest();
	if (!bStarted)
	{
		UE_LOG(LogUnrealHog, Warning, TEXT("PostHog batch request failed to start for %s"), *RequestUrl);

		CompleteOnce(State, false, 0, TEXT(""));

		return nullptr;
	}

	return MakeShared<FRequestHandle>(Request);
}

FString FPostHogHttpClient::GetBatchUrl() const
{
	return FString::Printf(TEXT("%s/batch"), *Host);
}

FString FPostHogHttpClient::NormalizeHost(const FString& InHost)
{
	FString NormalizedHost = InHost.TrimStartAndEnd();
	NormalizedHost.RemoveFromEnd(TEXT("/"));
	
	return NormalizedHost;
}

bool FPostHogHttpClient::SerializeJsonObject(const TSharedRef<FJsonObject>& JsonObject, FString& OutJson)
{
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
	return FJsonSerializer::Serialize(JsonObject, Writer);
}

bool FPostHogHttpClient::IsSuccessfulResponse(FHttpResponsePtr Response, bool bRequestSucceeded)
{
	if (!bRequestSucceeded || !Response.IsValid())
	{
		return false;
	}
	
	const int32 StatusCode = Response->GetResponseCode();
	return StatusCode >= 200 && StatusCode < 300;
}
