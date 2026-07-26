#pragma once

#include "CoreMinimal.h"
#include "FeatureFlags/PostHogFeatureFlagTransport.h"
#include "Http.h"
#include "HAL/CriticalSection.h"
#include "Templates/SharedPointer.h"

/**
 * @brief Feature-flag transport for `POST <canonical-host>/flags/?v=2` with the reference's bounded,
 * flag-specific retry policy.
 *
 * Both the per-attempt request and the retry delay are injectable seams, so retry counts, delays,
 * classification, and cancellation can be verified with fakes and never touch FHttpModule, a socket,
 * or a PostHog project. Production callers use the host constructor, which sends real HTTP attempts
 * and schedules retries on FTSTicker.
 *
 * Each fetch owns its retry state and keeps itself alive only while an attempt or delay is pending.
 * Cancellation (explicit, via CancelAll, or via transport destruction) suppresses the completion
 * callback and releases the pending attempt, so a late platform callback can never reach a released
 * owner. Every fetch that is not cancelled completes exactly once.
 */
class FPostHogFeatureFlagHttpTransport final : public IPostHogFeatureFlagTransport
{
public:
	using FOnAttemptComplete = TFunction<void(const FPostHogFeatureFlagAttemptOutcome&)>;

	// Starts a single attempt. Returns null when the attempt could not start, in which case it must
	// already have reported a failure outcome through the callback.
	using FAttemptFactory = TFunction<TSharedPtr<IPostHogFeatureFlagAttempt>(const FPostHogFeatureFlagRequest&, FOnAttemptComplete)>;

	// Schedules OnElapsed after DelaySeconds and returns a closure that cancels it.
	using FRetryScheduler = TFunction<TFunction<void()>(float DelaySeconds, TFunction<void()> OnElapsed)>;

	// Seam allowing tests to substitute a fake IHttpRequest instead of the real FHttpModule stack.
	using FHttpRequestFactory = TFunction<FHttpRequestRef()>;

	static constexpr float TimeoutSeconds = 10.0f;

	/**
	 * Everything the engine reports about one failed attempt that the reference's retry
	 * classification needs. EHttpFailureReason alone is too coarse (see ClassifyHttpFailure), so the
	 * classifier also uses whether the server actually started answering and how long the attempt ran.
	 */
	struct FHttpFailureContext
	{
		EHttpFailureReason EngineReason = EHttpFailureReason::None;

		/**
		 * True only when the server demonstrably began answering: a status line, a response header, or
		 * received body bytes was observed. The mere existence of a response object is not evidence —
		 * Unreal's Apple backend creates FAppleHttpResponse in FAppleHttpRequest::ProcessRequest,
		 * before any network I/O, and keeps it for non-connection failures — so this is fed from the
		 * request's own status/header/progress observations (see StartHttpAttempt).
		 */
		bool bServerStartedResponding = false;

		/**
		 * True when request bytes demonstrably left the client (observed from the attempt's own
		 * upload-progress reports). Curl only reads the request body after the connection and any TLS
		 * handshake have succeeded and the request headers are on the wire, so this is the engine's
		 * proof that a connection existed. It is what separates the reference's retryable reset/EOF
		 * class from the terminal failures that share EHttpFailureReason::Other/None: a drop before the
		 * server answered but after the request was sent is a reset or unexpected EOF, whereas
		 * certificate-verification, DNS and handshake failures never get that far.
		 */
		bool bRequestBodySent = false;

		/** How long the attempt ran before failing (IHttpRequest::GetElapsedTime). */
		float ElapsedSeconds = 0.0f;

		/** The engine's connection timeout for this attempt; 0 when unknown. */
		float ConnectTimeoutSeconds = 0.0f;
	};

	// Maps one failed attempt onto the reference's retry classes. Exposed so the concrete HTTP
	// mapping is directly testable, not just the policy it feeds. The failed attempt's HTTP status is
	// carried separately and gates retries (the reference never retries a connection failure whose
	// status is nonzero).
	static EPostHogFeatureFlagFailureReason ClassifyHttpFailure(const FHttpFailureContext& Failure);

	// Production constructor: real (or injected) HTTP requests against Host, FTSTicker retry delays.
	// InMaxRetries is the number of retries after the initial attempt (Unity's
	// FeatureFlagRequestMaxRetries); negative values are treated as zero.
	FPostHogFeatureFlagHttpTransport(const FString& InHost, int32 InMaxRetries, FHttpRequestFactory InRequestFactory = nullptr);

	// Test constructor: fully injected attempt and retry-delay seams.
	FPostHogFeatureFlagHttpTransport(int32 InMaxRetries, FAttemptFactory InAttemptFactory, FRetryScheduler InRetryScheduler = nullptr);

	virtual ~FPostHogFeatureFlagHttpTransport() override;

	virtual TSharedPtr<IPostHogFeatureFlagFetchHandle> Fetch(const FPostHogFeatureFlagRequest& Request, FOnFetchComplete OnComplete) override;
	virtual void CancelAll() override;

	// Number of attempts a fetch may issue in total, including the initial one.
	int32 GetMaxAttempts() const { return MaxAttempts; }

	// Per-fetch retry state, defined in the implementation file and owned by its pending
	// attempt/delay closures, so it outlives the caller's handle but never the transport's
	// cancellation. Named here only so the fetch handle can hold a weak reference to it.
	class FOperation;

private:
	static TSharedPtr<IPostHogFeatureFlagAttempt> StartHttpAttempt(const FString& Host,
		const FHttpRequestFactory& RequestFactory,
		const FPostHogFeatureFlagRequest& Request,
		FOnAttemptComplete OnComplete);

	static TFunction<void()> ScheduleRetryWithTicker(float DelaySeconds, TFunction<void()> OnElapsed);

	static int32 ResolveMaxAttempts(int32 InMaxRetries);

	FAttemptFactory AttemptFactory;
	FRetryScheduler RetryScheduler;
	int32 MaxAttempts;

	mutable FCriticalSection OperationsLock;
	TArray<TWeakPtr<FOperation, ESPMode::ThreadSafe>> Operations;
};
