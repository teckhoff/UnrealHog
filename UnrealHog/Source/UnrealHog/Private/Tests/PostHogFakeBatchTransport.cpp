// Trevor Eckhoff, 2026. All rights reserved.

#include "Tests/PostHogFakeBatchTransport.h"

#if WITH_DEV_AUTOMATION_TESTS

TSharedPtr<IPostHogBatchRequestHandle> FPostHogFakeBatchTransport::SendBatch(const FPostHogBatchPayload& Payload, FOnSendComplete OnComplete)
{
	if (bSynchronousFailure)
	{
		if (OnComplete)
		{
			OnComplete(false, 0, TEXT(""));
		}

		return nullptr;
	}

	const TSharedRef<bool> CancelledFlag = MakeShared<bool>(false);
	Pending.Add(FPendingCall{ MakeShared<FPostHogBatchPayload>(Payload), MoveTemp(OnComplete), CancelledFlag });

	return MakeShared<FFakeRequestHandle>(CancelledFlag);
}

int32 FPostHogFakeBatchTransport::GetSentCount() const
{
	return Pending.Num();
}

const FPostHogBatchPayload& FPostHogFakeBatchTransport::GetLastPayload() const
{
	check(Pending.Num() > 0);
	return *Pending.Last().Payload;
}

bool FPostHogFakeBatchTransport::IsLastRequestCancelled() const
{
	check(Pending.Num() > 0);
	return *Pending.Last().bCancelledFlag;
}

void FPostHogFakeBatchTransport::CompleteLast(bool bSuccess, int32 StatusCode, const FString& ResponseBody)
{
	check(Pending.Num() > 0);

	const FPendingCall Call = Pending.Last();
	Pending.RemoveAt(Pending.Num() - 1);

	if (*Call.bCancelledFlag)
	{
		return;
	}

	if (Call.OnComplete)
	{
		Call.OnComplete(bSuccess, StatusCode, ResponseBody);
	}
}

void FPostHogFakeBatchTransport::SetSynchronousFailure(bool bFail)
{
	bSynchronousFailure = bFail;
}

#endif // WITH_DEV_AUTOMATION_TESTS
