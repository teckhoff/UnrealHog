// Trevor Eckhoff, 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Http.h"

class FJsonObject;
struct FPostHogBatchPayload;

/**
 * @brief Minimal HTTP client for sending PostHog API requests.
 */
class FPostHogHttpClient
{
public:
	using FOnRequestComplete = TFunction<void(bool bSuccess, int32 StatusCode, const FString& ResponseBody)>;
	
	explicit FPostHogHttpClient(const FString& InHost);
	
	FHttpRequestPtr SendBatch(const FPostHogBatchPayload& Payload, FOnRequestComplete OnComplete) const;
	
private:
	static constexpr float TimeoutSeconds = 10.0f;
	
	FString Host;
	
	FString GetBatchUrl() const;
	static FString NormalizeHost(const FString& InHost);
	static bool SerializeJsonObject(const TSharedRef<FJsonObject>& JsonObject, FString& OutJson);
	static bool IsSuccessfulResponse(FHttpResponsePtr Response, bool bRequestSucceeded);
};
