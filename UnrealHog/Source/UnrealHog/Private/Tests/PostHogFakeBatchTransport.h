// Trevor Eckhoff, 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Events/PostHogBatchPayload.h"
#include "Http/PostHogBatchTransport.h"

/**
 * @brief Deterministic fake transport for Automation tests, letting queue tests complete or
 * cancel requests without constructing FHttpModule or contacting a network.
 */
class FPostHogFakeBatchTransport final : public IPostHogBatchTransport
{
public:
	virtual TSharedPtr<IPostHogBatchRequestHandle> SendBatch(const FPostHogBatchPayload& Payload, FOnSendComplete OnComplete) override;

	int32 GetSentCount() const;
	const FPostHogBatchPayload& GetLastPayload() const;
	bool IsLastRequestCancelled() const;

	// Invokes the stored completion callback for the most recently sent request, unless it was cancelled.
	void CompleteLast(bool bSuccess, int32 StatusCode, const FString& ResponseBody);

	// When true, SendBatch immediately completes with (false, 0, "") and returns no handle,
	// mirroring the synchronous serialization-failure path in FPostHogHttpClient::SendBatch.
	void SetSynchronousFailure(bool bFail);

private:
	class FFakeRequestHandle final : public IPostHogBatchRequestHandle
	{
	public:
		explicit FFakeRequestHandle(const TSharedRef<bool>& InCancelledFlag) : CancelledFlag(InCancelledFlag) {}

		virtual void Cancel() override { *CancelledFlag = true; }

	private:
		TSharedRef<bool> CancelledFlag;
	};

	struct FPendingCall
	{
		TSharedRef<FPostHogBatchPayload> Payload;
		FOnSendComplete OnComplete;
		TSharedRef<bool> bCancelledFlag;
	};

	TArray<FPendingCall> Pending;
	bool bSynchronousFailure = false;
};

#endif // WITH_DEV_AUTOMATION_TESTS
