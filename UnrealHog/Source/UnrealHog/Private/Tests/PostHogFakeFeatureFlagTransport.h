#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "FeatureFlags/PostHogFeatureFlagHttpTransport.h"
#include "FeatureFlags/PostHogFeatureFlagRequest.h"
#include "FeatureFlags/PostHogFeatureFlagTransport.h"

/**
 * @brief Deterministic stand-in for one feature-flag attempt.
 *
 * Never performs I/O: tests inspect the request it was handed and decide when and how the attempt
 * finishes. Cancel() clears the live callback exactly as a cancelled HTTP request would, but the
 * original callback is also retained separately so tests can force a late callback and prove the
 * transport's completion guard (not the delegate unbinding) is what prevents delivery into
 * released state.
 */
class FPostHogFakeFeatureFlagAttempt final : public IPostHogFeatureFlagAttempt
{
public:
	using FOnAttemptComplete = FPostHogFeatureFlagHttpTransport::FOnAttemptComplete;

	FPostHogFakeFeatureFlagAttempt(const FPostHogFeatureFlagRequest& InRequest, FOnAttemptComplete InOnComplete)
		: Request(InRequest)
		, OnComplete(InOnComplete)
		, RetainedOnComplete(MoveTemp(InOnComplete))
	{
	}

	virtual void Cancel() override
	{
		bCancelled = true;
		OnComplete = nullptr;
	}

	/** Completes as the transport's real attempt would; a no-op once cancelled. */
	void Complete(const FPostHogFeatureFlagAttemptOutcome& Outcome)
	{
		if (OnComplete)
		{
			OnComplete(Outcome);
		}
	}

	void CompleteWithSuccess(int32 StatusCode, const FString& Body)
	{
		Complete(FPostHogFeatureFlagAttemptOutcome::Success(StatusCode, Body));
	}

	void CompleteWithFailure(EPostHogFeatureFlagFailureReason Reason, int32 StatusCode = 0)
	{
		Complete(FPostHogFeatureFlagAttemptOutcome::Failure(Reason, StatusCode));
	}

	/** Delivers a callback even after cancellation, simulating a late platform callback. */
	void ForceLateCompletion(const FPostHogFeatureFlagAttemptOutcome& Outcome)
	{
		if (RetainedOnComplete)
		{
			RetainedOnComplete(Outcome);
		}
	}

	const FPostHogFeatureFlagRequest& GetRequest() const { return Request; }
	bool WasCancelled() const { return bCancelled; }

private:
	FPostHogFeatureFlagRequest Request;
	FOnAttemptComplete OnComplete;
	FOnAttemptComplete RetainedOnComplete;
	bool bCancelled = false;
};

/**
 * @brief Attempt factory that hands out FPostHogFakeFeatureFlagAttempt instances and records every
 * attempt the transport started, so retry counts and per-attempt request contents are observable.
 *
 * When bStartSynchronouslyFails is set, the next attempt reports the configured failure through the
 * callback before returning null, mirroring a request that never started.
 */
class FPostHogFakeFeatureFlagAttemptFactory
{
public:
	FPostHogFeatureFlagHttpTransport::FAttemptFactory MakeFactory()
	{
		return [this](const FPostHogFeatureFlagRequest& Request, FPostHogFeatureFlagHttpTransport::FOnAttemptComplete OnComplete)
			-> TSharedPtr<IPostHogFeatureFlagAttempt>
		{
			++StartCount;

			if (bStartSynchronouslyFails)
			{
				RequestedRequests.Add(Request);
				OnComplete(FPostHogFeatureFlagAttemptOutcome::Failure(SynchronousFailureReason));
				return nullptr;
			}

			const TSharedRef<FPostHogFakeFeatureFlagAttempt> Attempt = MakeShared<FPostHogFakeFeatureFlagAttempt>(Request, MoveTemp(OnComplete));
			RequestedRequests.Add(Request);
			Attempts.Add(Attempt);
			return Attempt;
		};
	}

	FPostHogFakeFeatureFlagAttempt& Last() const
	{
		check(Attempts.Num() > 0);
		return *Attempts.Last();
	}

	int32 Num() const { return Attempts.Num(); }

	TArray<TSharedRef<FPostHogFakeFeatureFlagAttempt>> Attempts;
	TArray<FPostHogFeatureFlagRequest> RequestedRequests;
	int32 StartCount = 0;
	bool bStartSynchronouslyFails = false;
	EPostHogFeatureFlagFailureReason SynchronousFailureReason = EPostHogFeatureFlagFailureReason::Other;
};

/**
 * @brief Fake retry clock: records requested delays and fires them only when a test says so, so
 * backoff timing is asserted without any real waiting.
 */
class FPostHogFakeRetryClock
{
public:
	struct FScheduledDelay
	{
		float DelaySeconds = 0.0f;
		TFunction<void()> OnElapsed;
		bool bCancelled = false;
		bool bFired = false;
	};

	FPostHogFeatureFlagHttpTransport::FRetryScheduler MakeScheduler()
	{
		return [this](float DelaySeconds, TFunction<void()> OnElapsed) -> TFunction<void()>
		{
			const TSharedRef<FScheduledDelay> Entry = MakeShared<FScheduledDelay>();
			Entry->DelaySeconds = DelaySeconds;
			Entry->OnElapsed = MoveTemp(OnElapsed);
			Scheduled.Add(Entry);

			return [this, Entry]()
			{
				Entry->bCancelled = true;
				Entry->OnElapsed = nullptr;
				++CancelCount;
			};
		};
	}

	/** Fires the oldest pending delay. Returns false when there is nothing to fire. */
	bool FireNext()
	{
		for (const TSharedRef<FScheduledDelay>& Entry : Scheduled)
		{
			if (!Entry->bFired && !Entry->bCancelled)
			{
				Entry->bFired = true;
				if (Entry->OnElapsed)
				{
					Entry->OnElapsed();
				}
				return true;
			}
		}

		return false;
	}

	TArray<float> GetDelays() const
	{
		TArray<float> Delays;
		for (const TSharedRef<FScheduledDelay>& Entry : Scheduled)
		{
			Delays.Add(Entry->DelaySeconds);
		}
		return Delays;
	}

	int32 Num() const { return Scheduled.Num(); }

	TArray<TSharedRef<FScheduledDelay>> Scheduled;
	int32 CancelCount = 0;
};

/**
 * @brief Fake IPostHogFeatureFlagTransport for consent-gating tests: records every fetch request it
 * was handed and never issues an attempt, HTTP request, or callback of its own.
 */
class FPostHogFakeFeatureFlagTransport final : public IPostHogFeatureFlagTransport
{
public:
	virtual TSharedPtr<IPostHogFeatureFlagFetchHandle> Fetch(const FPostHogFeatureFlagRequest& Request, FOnFetchComplete OnComplete) override
	{
		Requests.Add(Request);
		PendingCallbacks.Add(MoveTemp(OnComplete));
		return MakeShared<FHandle>(*this, PendingCallbacks.Num() - 1);
	}

	virtual void CancelAll() override
	{
		++CancelAllCount;
		if (ExternalCancelAllCount != nullptr)
		{
			// Survives this transport's destruction, so a test can assert that opt-out cancelled
			// in-flight fetches before releasing the transport.
			++(*ExternalCancelAllCount);
		}
		for (FOnFetchComplete& Callback : PendingCallbacks)
		{
			Callback = nullptr;
		}
	}

	/** Delivers a completion for the fetch at Index; a no-op once cancelled. */
	void CompleteFetch(int32 Index, const FPostHogFeatureFlagFetchResult& Result)
	{
		if (PendingCallbacks.IsValidIndex(Index) && PendingCallbacks[Index])
		{
			PendingCallbacks[Index](Result);
		}
	}

	TArray<FPostHogFeatureFlagRequest> Requests;
	TArray<FOnFetchComplete> PendingCallbacks;
	int32* ExternalCancelAllCount = nullptr;
	int32 CancelAllCount = 0;
	int32 CancelledHandleCount = 0;

private:
	class FHandle final : public IPostHogFeatureFlagFetchHandle
	{
	public:
		FHandle(FPostHogFakeFeatureFlagTransport& InOwner, int32 InIndex) : Owner(InOwner), Index(InIndex) {}

		virtual void Cancel() override
		{
			++Owner.CancelledHandleCount;
			if (Owner.PendingCallbacks.IsValidIndex(Index))
			{
				Owner.PendingCallbacks[Index] = nullptr;
			}
		}

	private:
		FPostHogFakeFeatureFlagTransport& Owner;
		int32 Index;
	};
};

#endif // WITH_DEV_AUTOMATION_TESTS
