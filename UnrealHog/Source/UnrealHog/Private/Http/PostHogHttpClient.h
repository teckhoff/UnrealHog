#pragma once

#include "CoreMinimal.h"
#include "Http.h"
#include "Http/PostHogBatchTransport.h"

class FJsonObject;
struct FPostHogBatchPayload;

/**
 * @brief Minimal HTTP client for sending PostHog API requests.
 */
class FPostHogHttpClient final : public IPostHogBatchTransport
{
public:
	explicit FPostHogHttpClient(const FString& InHost);

	virtual TSharedPtr<IPostHogBatchRequestHandle> SendBatch(const FPostHogBatchPayload& Payload, FOnSendComplete OnComplete) override;

private:
	class FRequestHandle final : public IPostHogBatchRequestHandle
	{
	public:
		explicit FRequestHandle(const FHttpRequestPtr& InRequest) : Request(InRequest) {}

		virtual void Cancel() override;

	private:
		FHttpRequestPtr Request;
	};

	static constexpr float TimeoutSeconds = 10.0f;

	FString Host;

	FString GetBatchUrl() const;
	static bool SerializeJsonObject(const TSharedRef<FJsonObject>& JsonObject, FString& OutJson);
	static bool IsSuccessfulResponse(FHttpResponsePtr Response, bool bRequestSucceeded);
};
