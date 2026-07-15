
#include "Http/PostHogHttpClient.h"

#include "Dom/JsonObject.h"
#include "Events/PostHogBatchPayload.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Logging/PostHogLogger.h"
#include "SDK/PostHogSdkInfo.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"


FPostHogHttpClient::FPostHogHttpClient(const FString& InHost)
	: Host(NormalizeHost(InHost))
{
}

void FPostHogHttpClient::FRequestHandle::Cancel()
{
	Request->OnProcessRequestComplete().Unbind();
	Request->CancelRequest();
}

TSharedPtr<IPostHogBatchRequestHandle> FPostHogHttpClient::SendBatch(const FPostHogBatchPayload& Payload, FOnSendComplete OnComplete)
{
	FString JsonBody;
	if (!SerializeJsonObject(Payload.ToJsonObject(), JsonBody))
	{
		UE_LOG(LogPostHog, Warning, TEXT("Failed to serialize PostHog batch payload"));
		
		if (OnComplete)
		{
			OnComplete(false, 0, TEXT(""));
		}
		
		return nullptr;
	}
	
	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->SetVerb(TEXT("POST"));
	Request->SetURL(GetBatchUrl());
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Request->SetHeader(TEXT("User-Agent"), FPostHogSdkInfo::GetUserAgent());
	Request->SetContentAsString(JsonBody);
	Request->SetTimeout(TimeoutSeconds);
	
	const FString RequestUrl = Request->GetURL();
	UE_LOG(LogPostHog, Verbose, TEXT("Sending PostHog batch to %s"), *RequestUrl);
	
	Request->OnProcessRequestComplete().BindLambda(
		[OnComplete = MoveTemp(OnComplete), RequestUrl](FHttpRequestPtr, FHttpResponsePtr Response, bool bRequestSucceeded) mutable
		{
			const bool bSuccess = FPostHogHttpClient::IsSuccessfulResponse(Response, bRequestSucceeded);
			const int32 StatusCode = Response.IsValid() ? Response->GetResponseCode() : 0;
			const FString ResponseBody = Response.IsValid() ? Response->GetContentAsString() : TEXT("");
			
			if (bSuccess)
			{
				UE_LOG(LogPostHog, Verbose, TEXT("PostHog batch sent successfully (status: %d)"), StatusCode);
			}
			else
			{
				UE_LOG(LogPostHog, Warning, TEXT("PostHog batch send failed for %s (status: %d)"), *RequestUrl, StatusCode);
			}
			
			if (OnComplete)
			{
				OnComplete(bSuccess, StatusCode, ResponseBody);
			}
		});
	
	Request->ProcessRequest();

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
