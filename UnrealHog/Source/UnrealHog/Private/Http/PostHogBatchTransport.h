// Trevor Eckhoff, 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"

struct FPostHogBatchPayload;

/**
 * @brief Handle to an in-flight batch send, allowing the owner to cancel it.
 */
class IPostHogBatchRequestHandle
{
public:
	virtual ~IPostHogBatchRequestHandle() = default;

	virtual void Cancel() = 0;
};

/**
 * @brief Seam between the event queue and the concrete HTTP transport, so queue behavior
 * can be tested without Unreal's live HTTP stack.
 */
class IPostHogBatchTransport
{
public:
	using FOnSendComplete = TFunction<void(bool bSuccess, int32 StatusCode, const FString& ResponseBody)>;

	virtual ~IPostHogBatchTransport() = default;

	virtual TSharedPtr<IPostHogBatchRequestHandle> SendBatch(const FPostHogBatchPayload& Payload, FOnSendComplete OnComplete) = 0;
};
