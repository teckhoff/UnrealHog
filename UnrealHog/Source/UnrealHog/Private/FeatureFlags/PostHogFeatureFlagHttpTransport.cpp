#include "FeatureFlags/PostHogFeatureFlagHttpTransport.h"

#include <atomic>

#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Http/PostHogEndpointUrls.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Logging/PostHogLogger.h"
#include "Misc/ScopeLock.h"
#include "SDK/PostHogSdkInfo.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	// Upper bound on configured retries, so an extreme setting cannot overflow the attempt count or
	// retain an operation indefinitely.
	constexpr int32 MaxConfigurableRetries = 1000000;

	// Slack allowed when matching a failed attempt's elapsed time against the engine's connection
	// timeout. Curl reports the timeout marginally early or late; connection refusal, DNS and
	// TLS-handshake failures return orders of magnitude faster, so no realistic failure sits inside
	// this band by accident.
	constexpr float ConnectTimeoutToleranceSeconds = 0.25f;

	// True when a failed attempt that never received anything ran for the engine's whole connection
	// timeout, which is what separates curl's connect timeout from a fast connection refusal.
	bool HasReachedConnectTimeout(const FPostHogFeatureFlagHttpTransport::FHttpFailureContext& Failure)
	{
		return Failure.ConnectTimeoutSeconds > 0.0f
			&& Failure.ElapsedSeconds >= Failure.ConnectTimeoutSeconds - ConnectTimeoutToleranceSeconds;
	}

	// Single owner of one attempt's completion callback, shared by every path that can complete it
	// (serialization failure, synchronous start failure, and the engine's completion delegate), so a
	// late platform callback after a synchronous failure cannot report a second outcome.
	struct FAttemptCompletionState
	{
		std::atomic<bool> bCompleted{false};

		// Set from the engine's status-line, header, and receive-progress observations, which are the
		// only backend-neutral proof that the server actually started answering. See
		// FHttpFailureContext::bServerStartedResponding.
		std::atomic<bool> bServerStartedResponding{false};

		// Set from the engine's upload-progress reports. See FHttpFailureContext::bRequestBodySent.
		std::atomic<bool> bRequestBodySent{false};

		FPostHogFeatureFlagHttpTransport::FOnAttemptComplete OnComplete;
	};

	void CompleteAttemptOnce(const TSharedRef<FAttemptCompletionState>& State, const FPostHogFeatureFlagAttemptOutcome& Outcome)
	{
		bool bExpected = false;
		if (State->bCompleted.compare_exchange_strong(bExpected, true) && State->OnComplete)
		{
			State->OnComplete(Outcome);
		}
	}

	// Parses a successful response body into the private response model. Any malformed body is a
	// terminal data-processing failure, never a retry.
	bool TryParseResponse(const FString& Body, FPostHogFeatureFlagsResponse& OutResponse)
	{
		TSharedPtr<FJsonObject> RootObject;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
		if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
		{
			return false;
		}

		TOptional<FPostHogFeatureFlagsResponse> Parsed = FPostHogFeatureFlagsResponse::FromJson(RootObject);
		if (!Parsed.IsSet())
		{
			return false;
		}

		OutResponse = MoveTemp(Parsed.GetValue());
		return true;
	}

	// One in-flight HTTP attempt. Cancel() unbinds before cancelling so the engine's completion
	// delegate can never run against a released owner.
	class FHttpAttempt final : public IPostHogFeatureFlagAttempt
	{
	public:
		explicit FHttpAttempt(const FHttpRequestPtr& InRequest) : Request(InRequest) {}

		virtual void Cancel() override
		{
			if (Request.IsValid())
			{
				Request->OnProcessRequestComplete().Unbind();
				Request->OnStatusCodeReceived().Unbind();
				Request->OnHeaderReceived().Unbind();
				Request->OnRequestProgress64().Unbind();
				Request->CancelRequest();
			}
		}

	private:
		FHttpRequestPtr Request;
	};
}

/**
 * Retry state for one fetch. Kept alive by whichever attempt callback or retry delay is pending, so
 * the caller may drop its handle, and released as soon as the fetch completes or is cancelled.
 *
 * Two locks, always taken in this order:
 *
 * - DeliveryLock is held across the whole claim-and-invoke sequence of a completion, across the
 *   creation and start of an attempt, and across the point where cancellation marks the operation
 *   finished (but never while cancellation waits on an engine timer or request). It makes Cancel() a barrier
 *   in both directions: a cancelling thread cannot return while a completion callback is in flight,
 *   and no attempt can be created or started once cancellation has returned, so an owner that
 *   cancels before releasing its state is never touched by a callback and never has a request issued
 *   on its behalf afterwards. Unreal's FCriticalSection is recursive, so an attempt that completes
 *   synchronously inside the factory, or a callback that cancels its own fetch, re-enters safely.
 * - StateLock guards the mutable retry state (attempt counter, active attempt, pending delay) that
 *   attempt callbacks touch from HTTP threads. It is never held while a callback runs.
 *
 * bFinished, guarded by StateLock, is the single arbiter of exactly-once completion: only the caller
 * that flips it may invoke the completion callback, and cancellation flips it without invoking
 * anything.
 */
class FPostHogFeatureFlagHttpTransport::FOperation : public TSharedFromThis<FPostHogFeatureFlagHttpTransport::FOperation, ESPMode::ThreadSafe>
{
public:
	FOperation(const FPostHogFeatureFlagRequest& InRequest,
		FOnFetchComplete InOnComplete,
		FAttemptFactory InAttemptFactory,
		FRetryScheduler InRetryScheduler,
		int32 InMaxAttempts)
		: Request(InRequest)
		, OnComplete(MoveTemp(InOnComplete))
		, AttemptFactory(MoveTemp(InAttemptFactory))
		, RetryScheduler(MoveTemp(InRetryScheduler))
		, MaxAttempts(InMaxAttempts)
	{
	}

	void Start()
	{
		StartAttempt();
	}

	bool IsFinished() const
	{
		FScopeLock Lock(&StateLock);
		return bFinished;
	}

	// Idempotent, and safe to call from any thread or from inside a completion callback. Blocks
	// until any completion callback already in flight has returned, and until any attempt already
	// being created has been started (and then cancelled). Once this returns, no callback can run
	// and no further attempt can be issued, so the caller can release the state the callback
	// captures.
	void Cancel()
	{
		TSharedPtr<IPostHogFeatureFlagAttempt> AttemptToCancel;
		TFunction<void()> DelayToCancel;

		{
			// The barrier itself: acquiring it waits out any completion delivery or attempt start
			// already in progress, and marking the operation finished under it stops any later one.
			FScopeLock DeliveryBarrier(&DeliveryLock);

			FScopeLock Lock(&StateLock);
			if (bFinished)
			{
				return;
			}

			bFinished = true;
			AttemptToCancel = MoveTemp(ActiveAttempt);
			ActiveAttempt.Reset();
			DelayToCancel = MoveTemp(CancelRetryDelay);
			CancelRetryDelay = nullptr;
			OnComplete = nullptr;
		}

		// Deliberately outside DeliveryLock: cancelling a scheduled delay blocks until an already
		// executing timer callback returns (FTSTicker::RemoveTicker waits for the ticker), and that
		// callback may itself be blocked acquiring DeliveryLock inside StartAttempt. Holding the lock
		// here would make the two wait on each other forever. It is safe to release it first because
		// bFinished is already set: the timer callback and the attempt's completion delegate can now
		// only observe a finished operation and return without touching the owner's state.
		if (AttemptToCancel.IsValid())
		{
			AttemptToCancel->Cancel();
		}

		if (DelayToCancel)
		{
			DelayToCancel();
		}
	}

private:
	void StartAttempt()
	{
		// Held across creation and start of the attempt, not just around the state bookkeeping: a
		// Cancel() that returns must guarantee no request can still be issued afterwards, which the
		// bFinished check below enforces only while cancellation cannot interleave with it.
		FScopeLock DeliveryBarrier(&DeliveryLock);

		int32 AttemptIndex = 0;
		{
			FScopeLock Lock(&StateLock);
			if (bFinished)
			{
				return;
			}

			CancelRetryDelay = nullptr;
			AttemptIndex = ++AttemptCount;
		}

		const TSharedRef<FOperation, ESPMode::ThreadSafe> Self = AsShared();
		// The attempt may complete synchronously inside the factory (start failure), so the returned
		// handle is only adopted if this attempt is still the current one afterwards.
		TSharedPtr<IPostHogFeatureFlagAttempt> Attempt = AttemptFactory(Request,
			[Self, AttemptIndex](const FPostHogFeatureFlagAttemptOutcome& Outcome)
			{
				Self->HandleAttemptOutcome(AttemptIndex, Outcome);
			});

		bool bCancelAttempt = false;
		{
			FScopeLock Lock(&StateLock);
			if (bFinished || AttemptIndex != AttemptCount)
			{
				bCancelAttempt = true;
			}
			else if (Attempt.IsValid())
			{
				ActiveAttempt = Attempt;
			}
		}

		if (bCancelAttempt && Attempt.IsValid())
		{
			Attempt->Cancel();
		}
	}

	// Parses a successful attempt's body into the fetch result; a malformed body is terminal.
	static FPostHogFeatureFlagFetchResult MakeParsedResult(int32 CompletedAttemptCount, const FPostHogFeatureFlagAttemptOutcome& Outcome)
	{
		FPostHogFeatureFlagFetchResult Result;
		Result.AttemptCount = CompletedAttemptCount;
		Result.StatusCode = Outcome.StatusCode;

		FPostHogFeatureFlagsResponse Parsed;
		if (TryParseResponse(Outcome.ResponseBody, Parsed))
		{
			Result.bSucceeded = true;
			Result.Response = MoveTemp(Parsed);
			return Result;
		}

		UE_LOG(LogUnrealHog, Warning, TEXT("PostHog feature flag response could not be parsed (status: %d)"), Outcome.StatusCode);
		Result.FailureReason = EPostHogFeatureFlagFailureReason::DataProcessing;
		return Result;
	}

	void HandleAttemptOutcome(int32 AttemptIndex, const FPostHogFeatureFlagAttemptOutcome& Outcome)
	{
		int32 CompletedAttemptCount = 0;
		{
			FScopeLock Lock(&StateLock);
			if (bFinished || AttemptIndex != AttemptCount)
			{
				// Cancelled, already completed, or a stale callback from a superseded attempt.
				return;
			}

			ActiveAttempt.Reset();
			CompletedAttemptCount = AttemptCount;
		}

		if (Outcome.bSucceeded)
		{
			Complete(MakeParsedResult(CompletedAttemptCount, Outcome));
			return;
		}

		const bool bCanRetry = PostHogFeatureFlagRetryPolicy::ShouldRetry(Outcome.FailureReason, Outcome.StatusCode)
			&& CompletedAttemptCount < MaxAttempts;

		if (!bCanRetry)
		{
			FPostHogFeatureFlagFetchResult Result;
			Result.AttemptCount = CompletedAttemptCount;
			Result.StatusCode = Outcome.StatusCode;
			Result.FailureReason = Outcome.FailureReason;
			Complete(Result);
			return;
		}

		ScheduleRetry(AttemptIndex, CompletedAttemptCount);
	}

	void ScheduleRetry(int32 AttemptIndex, int32 FailedAttempt)
	{
		const float DelaySeconds = PostHogFeatureFlagRetryPolicy::GetRetryDelaySeconds(FailedAttempt);
		// Routine, bounded, and self-correcting: logged at Log so a retried fetch does not read as a
		// fault the way the terminal parse/start failures below do.
		UE_LOG(LogUnrealHog, Log, TEXT("PostHog feature flag fetch failed; retrying attempt %d of %d in %.3fs"),
			FailedAttempt + 1, MaxAttempts, DelaySeconds);

		const TSharedRef<FOperation, ESPMode::ThreadSafe> Self = AsShared();
		TFunction<void()> Cancel = RetryScheduler(DelaySeconds,
			[Self, AttemptIndex]()
			{
				{
					FScopeLock Lock(&Self->StateLock);
					if (Self->bFinished || AttemptIndex != Self->AttemptCount)
					{
						return;
					}
				}

				Self->StartAttempt();
			});

		bool bCancelDelay = false;
		{
			FScopeLock Lock(&StateLock);
			// The scheduler may have fired synchronously, starting the next attempt already.
			if (bFinished || AttemptIndex != AttemptCount)
			{
				bCancelDelay = true;
			}
			else
			{
				CancelRetryDelay = MoveTemp(Cancel);
				Cancel = nullptr;
			}
		}

		if (bCancelDelay && Cancel)
		{
			Cancel();
		}
	}

	void Complete(const FPostHogFeatureFlagFetchResult& Result)
	{
		// Claimed and delivered under DeliveryLock so a concurrent Cancel() either wins the claim
		// (and no callback runs at all) or waits here until delivery has finished.
		FScopeLock DeliveryBarrier(&DeliveryLock);

		FOnFetchComplete Callback;
		{
			FScopeLock Lock(&StateLock);
			if (bFinished)
			{
				return;
			}

			bFinished = true;
			Callback = MoveTemp(OnComplete);
			OnComplete = nullptr;
			ActiveAttempt.Reset();
			CancelRetryDelay = nullptr;
		}

		if (Callback)
		{
			Callback(Result);
		}
	}

	FPostHogFeatureFlagRequest Request;
	FOnFetchComplete OnComplete;
	FAttemptFactory AttemptFactory;
	FRetryScheduler RetryScheduler;
	int32 MaxAttempts;

	// Outer lock; see the class comment for the ordering contract.
	FCriticalSection DeliveryLock;

	mutable FCriticalSection StateLock;
	bool bFinished = false;
	int32 AttemptCount = 0;
	TSharedPtr<IPostHogFeatureFlagAttempt> ActiveAttempt;
	TFunction<void()> CancelRetryDelay;
};

namespace
{
	// Handle held by the caller; weak so a discarded handle neither extends nor shortens the
	// operation's lifetime.
	class FFetchHandle final : public IPostHogFeatureFlagFetchHandle
	{
	public:
		explicit FFetchHandle(const TSharedPtr<FPostHogFeatureFlagHttpTransport::FOperation, ESPMode::ThreadSafe>& InOperation)
			: Operation(InOperation)
		{
		}

		virtual void Cancel() override
		{
			if (const TSharedPtr<FPostHogFeatureFlagHttpTransport::FOperation, ESPMode::ThreadSafe> Pinned = Operation.Pin())
			{
				Pinned->Cancel();
			}
		}

	private:
		TWeakPtr<FPostHogFeatureFlagHttpTransport::FOperation, ESPMode::ThreadSafe> Operation;
	};
}

FPostHogFeatureFlagHttpTransport::FPostHogFeatureFlagHttpTransport(const FString& InHost, int32 InMaxRetries, FHttpRequestFactory InRequestFactory)
	: MaxAttempts(ResolveMaxAttempts(InMaxRetries))
{
	const FString CanonicalHost = PostHogEndpointUrls::NormalizeHost(InHost);
	FHttpRequestFactory RequestFactory = InRequestFactory
		? MoveTemp(InRequestFactory)
		: FHttpRequestFactory([]() { return FHttpModule::Get().CreateRequest(); });

	AttemptFactory = [CanonicalHost, RequestFactory](const FPostHogFeatureFlagRequest& Request, FOnAttemptComplete OnComplete)
	{
		return StartHttpAttempt(CanonicalHost, RequestFactory, Request, MoveTemp(OnComplete));
	};

	RetryScheduler = &FPostHogFeatureFlagHttpTransport::ScheduleRetryWithTicker;
}

FPostHogFeatureFlagHttpTransport::FPostHogFeatureFlagHttpTransport(int32 InMaxRetries, FAttemptFactory InAttemptFactory, FRetryScheduler InRetryScheduler)
	: AttemptFactory(MoveTemp(InAttemptFactory))
	, RetryScheduler(InRetryScheduler ? MoveTemp(InRetryScheduler) : FRetryScheduler(&FPostHogFeatureFlagHttpTransport::ScheduleRetryWithTicker))
	, MaxAttempts(ResolveMaxAttempts(InMaxRetries))
{
}

FPostHogFeatureFlagHttpTransport::~FPostHogFeatureFlagHttpTransport()
{
	CancelAll();
}

int32 FPostHogFeatureFlagHttpTransport::ResolveMaxAttempts(int32 InMaxRetries)
{
	return FMath::Clamp(InMaxRetries, 0, MaxConfigurableRetries) + 1;
}

TSharedPtr<IPostHogFeatureFlagFetchHandle> FPostHogFeatureFlagHttpTransport::Fetch(const FPostHogFeatureFlagRequest& Request, FOnFetchComplete OnComplete)
{
	check(AttemptFactory);

	const TSharedRef<FOperation, ESPMode::ThreadSafe> Operation = MakeShared<FOperation, ESPMode::ThreadSafe>(
		Request, MoveTemp(OnComplete), AttemptFactory, RetryScheduler, MaxAttempts);

	{
		FScopeLock Lock(&OperationsLock);
		Operations.RemoveAll([](const TWeakPtr<FOperation, ESPMode::ThreadSafe>& Existing) { return !Existing.IsValid(); });
		Operations.Add(Operation);
	}

	Operation->Start();

	if (Operation->IsFinished())
	{
		// Serialization or start failure already reported the outcome; there is nothing left to
		// cancel, so the caller gets the documented null handle instead of an inert one.
		return nullptr;
	}

	return MakeShared<FFetchHandle>(TSharedPtr<FOperation, ESPMode::ThreadSafe>(Operation));
}

void FPostHogFeatureFlagHttpTransport::CancelAll()
{
	TArray<TSharedPtr<FOperation, ESPMode::ThreadSafe>> Pinned;
	{
		FScopeLock Lock(&OperationsLock);
		Pinned.Reserve(Operations.Num());
		for (const TWeakPtr<FOperation, ESPMode::ThreadSafe>& Weak : Operations)
		{
			if (TSharedPtr<FOperation, ESPMode::ThreadSafe> Operation = Weak.Pin())
			{
				Pinned.Add(MoveTemp(Operation));
			}
		}
		Operations.Reset();
	}

	for (const TSharedPtr<FOperation, ESPMode::ThreadSafe>& Operation : Pinned)
	{
		Operation->Cancel();
	}
}

EPostHogFeatureFlagFailureReason FPostHogFeatureFlagHttpTransport::ClassifyHttpFailure(const FHttpFailureContext& Failure)
{
	switch (Failure.EngineReason)
	{
	case EHttpFailureReason::TimedOut:
		// The SDK's own 10-second timeout: the reference's retryable "timed out" class.
		return EPostHogFeatureFlagFailureReason::Timeout;

	case EHttpFailureReason::Cancelled:
		return EPostHogFeatureFlagFailureReason::Cancelled;

	case EHttpFailureReason::ResponseTooLarge:
		// The body cannot be consumed; retrying would fail identically.
		return EPostHogFeatureFlagFailureReason::DataProcessing;

	case EHttpFailureReason::ConnectionError:
		// The curl backend collapses connection refusal, DNS and TLS-handshake failures, curl's own
		// connect timeout (CURLE_OPERATION_TIMEDOUT), xcurl's send errors and the activity timeout
		// into this one value (FCurlHttpRequest::FinishRequest). The reference splits them, so the
		// remaining signals separate them:
		if (Failure.bServerStartedResponding || Failure.bRequestBodySent)
		{
			// A connection existed and carried traffic before it stalled or dropped: the reference's
			// retryable reset/EOF/connection-lost class.
			return EPostHogFeatureFlagFailureReason::ConnectionLost;
		}

		if (HasReachedConnectTimeout(Failure))
		{
			// Nothing arrived and the attempt consumed the whole connection timeout: a connect-phase
			// timeout, which the reference retries. Refusal, DNS and TLS failures return long before
			// this, so they never land here.
			return EPostHogFeatureFlagFailureReason::Timeout;
		}

		// Nothing arrived and the attempt failed fast: the reference's terminal "cannot connect to
		// destination host" class.
		return EPostHogFeatureFlagFailureReason::ConnectionFailed;

	default:
		// EHttpFailureReason::Other and None are the backend's catch-all: curl's mapping switch leaves
		// every other CURLcode unmapped (CurlHttp.cpp:1318-1333), after which the shared code labels it
		// Other (HttpRequestCommon.cpp:291-310). That bucket carries the transient drops the reference
		// retries (CURLE_RECV_ERROR, CURLE_PARTIAL_FILE, and CURLE_GOT_NOTHING, which strikes before any
		// status line) together with genuinely terminal failures (certificate verification, HTTP/2
		// protocol errors, local write errors, and the Apple backend's TLS and protocol errors).
		//
		// The separating signal is that the request reached the wire: TLS, certificate and HTTP/2
		// handshake failures abort before a single request byte is uploaded, whereas a reset or
		// unexpected EOF can only happen after curl has handed the body to the socket. Curl reports that
		// final upload progress from FinishRequest (CurlHttp.cpp:1274) while the request is still
		// Processing, before the failure is labelled and before the completion delegate runs
		// (CurlHttp.cpp:1337), so bRequestBodySent is already accurate here even for a POST that is reset
		// before the server says anything at all.
		//
		// Whatever remains ambiguous is settled by the reference's own `statusCode != 0` guard in
		// ShouldRetry: once a status line arrives, curl records the code (CurlHttp.cpp:1250-1263) and the
		// failure is terminal regardless of this classification, so post-status HTTP/2 and local write
		// errors never retry.
		return Failure.bServerStartedResponding || Failure.bRequestBodySent
			? EPostHogFeatureFlagFailureReason::ConnectionLost
			: EPostHogFeatureFlagFailureReason::Other;
	}
}

namespace
{
	// Configures one POST /flags/?v=2 request: JSON headers, SDK User-Agent, and the 10-second
	// timeout the reference uses for flag fetches.
	FHttpRequestRef BuildFlagsHttpRequest(const FString& Host,
		const FPostHogFeatureFlagHttpTransport::FHttpRequestFactory& RequestFactory,
		const FString& JsonBody)
	{
		FHttpRequestRef HttpRequest = RequestFactory();
		HttpRequest->SetVerb(TEXT("POST"));
		HttpRequest->SetURL(PostHogEndpointUrls::BuildFeatureFlagsUrl(Host));
		HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		HttpRequest->SetHeader(TEXT("Accept"), TEXT("application/json"));
		HttpRequest->SetHeader(TEXT("User-Agent"), FPostHogSdkInfo::GetUserAgent());
		HttpRequest->SetContentAsString(JsonBody);
		HttpRequest->SetTimeout(FPostHogFeatureFlagHttpTransport::TimeoutSeconds);
		return HttpRequest;
	}

	// True when the response object itself proves the server answered. Existence alone proves nothing
	// (the Apple backend creates the response before any network I/O), so this requires content the
	// server must have sent: a status line, a header, or body bytes. Backends that report neither
	// status nor progress callbacks are still classified correctly through this path.
	bool ResponseCarriesServerData(const FHttpResponsePtr& Response)
	{
		return Response.IsValid()
			&& (Response->GetResponseCode() > 0 || Response->GetContent().Num() > 0 || Response->GetAllHeaders().Num() > 0);
	}

	// Records, on the attempt's own state, how far the exchange got: the first sign that the server
	// began answering, and whether request bytes reached the wire. Every backend reports at least one
	// of the response signals before handing back response data, and none of them can fire before the
	// peer has sent something, which is what makes them safe positive signals where the response
	// object's existence is not. Curl reports the final upload progress from FinishRequest before it
	// runs the completion delegate, so a failed attempt's upload record is complete by the time it is
	// classified (see FHttpFailureContext::bServerStartedResponding and bRequestBodySent).
	void ObserveExchangeProgress(const FHttpRequestRef& HttpRequest, const TSharedRef<FAttemptCompletionState>& State)
	{
		HttpRequest->OnStatusCodeReceived().BindLambda([State](FHttpRequestPtr, int32)
		{
			State->bServerStartedResponding.store(true);
		});

		HttpRequest->OnHeaderReceived().BindLambda([State](FHttpRequestPtr, const FString&, const FString&)
		{
			State->bServerStartedResponding.store(true);
		});

		HttpRequest->OnRequestProgress64().BindLambda([State](FHttpRequestPtr, uint64 BytesSent, uint64 BytesReceived)
		{
			if (BytesSent > 0)
			{
				State->bRequestBodySent.store(true);
			}

			if (BytesReceived > 0)
			{
				State->bServerStartedResponding.store(true);
			}
		});
	}

	// The attempt's own record of what the engine reported while it ran, taken when the attempt
	// completes; see ObserveExchangeProgress.
	struct FExchangeObservations
	{
		bool bServerStartedResponding = false;
		bool bRequestBodySent = false;

		explicit FExchangeObservations(const FAttemptCompletionState& State)
			: bServerStartedResponding(State.bServerStartedResponding.load())
			, bRequestBodySent(State.bRequestBodySent.load())
		{
		}
	};

	// Turns one engine completion into the transport's attempt outcome.
	FPostHogFeatureFlagAttemptOutcome MakeAttemptOutcome(const FHttpRequestPtr& CompletedRequest,
		const FHttpResponsePtr& Response,
		bool bRequestSucceeded,
		const FExchangeObservations& Observations)
	{
		if (bRequestSucceeded && Response.IsValid())
		{
			const int32 StatusCode = Response->GetResponseCode();
			if (StatusCode >= 200 && StatusCode < 300)
			{
				return FPostHogFeatureFlagAttemptOutcome::Success(StatusCode, Response->GetContentAsString());
			}

			return FPostHogFeatureFlagAttemptOutcome::Failure(EPostHogFeatureFlagFailureReason::Protocol, StatusCode);
		}

		FPostHogFeatureFlagHttpTransport::FHttpFailureContext Failure;
		Failure.bServerStartedResponding = Observations.bServerStartedResponding || ResponseCarriesServerData(Response);
		Failure.bRequestBodySent = Observations.bRequestBodySent;
		Failure.ConnectTimeoutSeconds = FHttpModule::Get().GetHttpConnectionTimeout();
		if (CompletedRequest.IsValid())
		{
			Failure.EngineReason = CompletedRequest->GetFailureReason();
			Failure.ElapsedSeconds = CompletedRequest->GetElapsedTime();
		}

		// The status the server did manage to send, preserved so a drop after a real HTTP status is
		// terminal, exactly as the reference's `statusCode != 0` guard requires. Absent, unknown, and
		// negative codes all read as zero.
		const int32 StatusCode = Response.IsValid() ? FMath::Max(0, Response->GetResponseCode()) : 0;

		return FPostHogFeatureFlagAttemptOutcome::Failure(FPostHogFeatureFlagHttpTransport::ClassifyHttpFailure(Failure), StatusCode);
	}
}

TSharedPtr<IPostHogFeatureFlagAttempt> FPostHogFeatureFlagHttpTransport::StartHttpAttempt(const FString& Host,
	const FHttpRequestFactory& RequestFactory,
	const FPostHogFeatureFlagRequest& Request,
	FOnAttemptComplete OnComplete)
{
	const TSharedRef<FAttemptCompletionState> State = MakeShared<FAttemptCompletionState>();
	State->OnComplete = MoveTemp(OnComplete);

	FString JsonBody;
	if (!Request.ToJsonString(JsonBody))
	{
		UE_LOG(LogUnrealHog, Warning, TEXT("Failed to serialize PostHog feature flag request"));

		CompleteAttemptOnce(State, FPostHogFeatureFlagAttemptOutcome::Failure(EPostHogFeatureFlagFailureReason::DataProcessing));

		return nullptr;
	}

	const FHttpRequestRef HttpRequest = BuildFlagsHttpRequest(Host, RequestFactory, JsonBody);

	const FString RequestUrl = HttpRequest->GetURL();
	UE_LOG(LogUnrealHog, Verbose, TEXT("Fetching PostHog feature flags from %s"), *RequestUrl);

	ObserveExchangeProgress(HttpRequest, State);

	HttpRequest->OnProcessRequestComplete().BindLambda(
		[State](FHttpRequestPtr CompletedRequest, FHttpResponsePtr Response, bool bRequestSucceeded)
		{
			CompleteAttemptOnce(State,
				MakeAttemptOutcome(CompletedRequest, Response, bRequestSucceeded, FExchangeObservations(*State)));
		});

	if (!HttpRequest->ProcessRequest())
	{
		UE_LOG(LogUnrealHog, Warning, TEXT("PostHog feature flag request failed to start for %s"), *RequestUrl);

		CompleteAttemptOnce(State, FPostHogFeatureFlagAttemptOutcome::Failure(EPostHogFeatureFlagFailureReason::Other));

		return nullptr;
	}

	return MakeShared<FHttpAttempt>(HttpRequest);
}

TFunction<void()> FPostHogFeatureFlagHttpTransport::ScheduleRetryWithTicker(float DelaySeconds, TFunction<void()> OnElapsed)
{
	const FTSTicker::FDelegateHandle TickerHandle = FTSTicker::GetCoreTicker().AddTicker(TEXT("PostHogFeatureFlagRetry"), DelaySeconds,
		[OnElapsed](float) -> bool
		{
			OnElapsed();
			return false;
		});

	return [TickerHandle]()
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	};
}
