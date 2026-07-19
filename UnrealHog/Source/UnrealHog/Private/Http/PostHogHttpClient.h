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
	// Seam allowing tests to substitute a fake IHttpRequest instead of the real FHttpModule stack.
	using FRequestFactory = TFunction<FHttpRequestRef()>;

	explicit FPostHogHttpClient(const FString& InHost);
	FPostHogHttpClient(const FString& InHost, FRequestFactory InRequestFactory);

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
	FRequestFactory RequestFactory;

	FString GetBatchUrl() const;
	static FString NormalizeHost(const FString& InHost);
	static bool SerializeJsonObject(const TSharedRef<FJsonObject>& JsonObject, FString& OutJson);
	static bool IsSuccessfulResponse(FHttpResponsePtr Response, bool bRequestSucceeded);
};
