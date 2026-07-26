#pragma once

#include "CoreMinimal.h"
#include "FeatureFlags/Models/PostHogFeatureFlagsResponse.h"
#include "FeatureFlags/PostHogFeatureFlagRequest.h"
#include "Misc/Optional.h"
#include "Templates/Function.h"

/**
 * @brief Why a single feature-flag attempt failed.
 *
 * The reference (NetworkClient.ShouldRetryFeatureFlagsRequest) classifies retries from Unity's
 * UnityWebRequest.Result plus the platform error string: with a zero status it retries timeouts,
 * "reset", "eof", and "connection lost", but not "cannot connect to destination host".
 *
 * Unreal exposes neither the error string nor the CURLcode, and its EHttpFailureReason is coarser
 * than the reference's classes (see FCurlHttpRequest::FinishRequest in
 * Engine/Source/Runtime/Online/HTTP/Private/Curl/CurlHttp.cpp):
 *
 * - EHttpFailureReason::ConnectionError covers CURLE_COULDNT_CONNECT and
 *   CURLE_COULDNT_RESOLVE_HOST/PROXY and CURLE_SSL_CONNECT_ERROR (all terminal for the reference)
 *   together with CURLE_OPERATION_TIMEDOUT and the activity timeout (both retryable), and
 * - EHttpFailureReason::Other/None is the backend's default branch: the transient mid-exchange drops
 *   the reference retries (CURLE_RECV_ERROR, CURLE_PARTIAL_FILE, a response with no status line)
 *   alongside terminal failures such as certificate verification and HTTP/2 protocol errors.
 *
 * FPostHogFeatureFlagHttpTransport::ClassifyHttpFailure therefore separates them with three further
 * engine signals: whether the server demonstrably started answering (observed from the request's own
 * status-line, header, and receive-progress callbacks, never from the existence of a response object,
 * which the Apple backend creates before any network I/O), whether request bytes reached the wire
 * (upload progress, which only happens once the connection and any TLS handshake succeeded, so a drop
 * before the server answered is a reset or unexpected EOF rather than a failure to connect), and how
 * long the attempt ran relative to the engine's connection timeout.
 * The SDK's own 10-second timeout surfaces separately as EHttpFailureReason::TimedOut. Every
 * enumerator below is produced by the live HTTP path; none exists only for fakes.
 */
enum class EPostHogFeatureFlagFailureReason : uint8
{
	/** No failure (successful attempt). */
	None,

	/** The server answered with a non-2xx status; StatusCode carries it. */
	Protocol,

	/** The request timed out. Status-zero and retryable, mirroring the reference. */
	Timeout,

	/**
	 * An established connection dropped or answered nothing usable (connection reset, unexpected
	 * EOF, connection lost). Retryable only at status zero, mirroring the reference's retryable
	 * substrings and its `statusCode != 0` guard.
	 */
	ConnectionLost,

	/**
	 * The connection could not be established: refusal, DNS, or TLS handshake failure, which the
	 * curl backend reports together. Terminal, matching the reference's refusal handling.
	 */
	ConnectionFailed,

	/** The attempt was cancelled by the owner (opt-out, shutdown, or explicit cancel). */
	Cancelled,

	/** Request serialization, response parsing, or another local data-processing failure. */
	DataProcessing,

	/** Any other terminal failure (including a request that never started). */
	Other
};

/** Outcome of one feature-flag attempt, as reported by the attempt seam. */
struct FPostHogFeatureFlagAttemptOutcome
{
	bool bSucceeded = false;

	/** HTTP status code, or 0 when no response was received. */
	int32 StatusCode = 0;

	EPostHogFeatureFlagFailureReason FailureReason = EPostHogFeatureFlagFailureReason::None;

	/** Raw response body; only meaningful for a successful attempt. */
	FString ResponseBody;

	static FPostHogFeatureFlagAttemptOutcome Success(int32 InStatusCode, const FString& InResponseBody)
	{
		FPostHogFeatureFlagAttemptOutcome Outcome;
		Outcome.bSucceeded = true;
		Outcome.StatusCode = InStatusCode;
		Outcome.ResponseBody = InResponseBody;
		return Outcome;
	}

	static FPostHogFeatureFlagAttemptOutcome Failure(EPostHogFeatureFlagFailureReason InReason, int32 InStatusCode = 0)
	{
		FPostHogFeatureFlagAttemptOutcome Outcome;
		Outcome.bSucceeded = false;
		Outcome.StatusCode = InStatusCode;
		Outcome.FailureReason = InReason;
		return Outcome;
	}
};

/** Handle to one in-flight attempt, so the transport can cancel it. */
class IPostHogFeatureFlagAttempt
{
public:
	virtual ~IPostHogFeatureFlagAttempt() = default;

	// Must guarantee the attempt's completion callback is never invoked afterwards.
	virtual void Cancel() = 0;
};

/** Result of a whole fetch, after any bounded retries. */
struct FPostHogFeatureFlagFetchResult
{
	bool bSucceeded = false;

	/** Status code of the final attempt, or 0 when no response was received. */
	int32 StatusCode = 0;

	EPostHogFeatureFlagFailureReason FailureReason = EPostHogFeatureFlagFailureReason::None;

	/** Parsed response; set only on success. */
	TOptional<FPostHogFeatureFlagsResponse> Response;

	/** Number of attempts issued, including the initial one. */
	int32 AttemptCount = 0;
};

/** Handle to an in-flight fetch (including any pending retry delay), allowing the owner to cancel it. */
class IPostHogFeatureFlagFetchHandle
{
public:
	virtual ~IPostHogFeatureFlagFetchHandle() = default;

	// Idempotent. Suppresses the completion callback and cancels any active attempt or retry delay.
	// Once Cancel() returns, no completion callback for this fetch can begin or still be running,
	// and no further request can be issued for it, so the owner may release the state the callback
	// captures.
	virtual void Cancel() = 0;
};

/**
 * @brief Seam between feature-flag callers and the concrete HTTP transport, so fetch behavior can
 * be verified without Unreal's live HTTP stack or a PostHog project.
 */
class IPostHogFeatureFlagTransport
{
public:
	using FOnFetchComplete = TFunction<void(const FPostHogFeatureFlagFetchResult&)>;

	virtual ~IPostHogFeatureFlagTransport() = default;

	// Starts a fetch. OnComplete runs exactly once unless the fetch is cancelled, in which case it
	// never runs. Returns null when the fetch already completed synchronously (e.g. serialization
	// failure), so a null handle is never an error the caller must handle separately.
	virtual TSharedPtr<IPostHogFeatureFlagFetchHandle> Fetch(const FPostHogFeatureFlagRequest& Request, FOnFetchComplete OnComplete) = 0;

	// Cancels every in-flight fetch owned by this transport, suppressing their callbacks.
	virtual void CancelAll() = 0;
};

namespace PostHogFeatureFlagRetryPolicy
{
	/** Initial retry delay; doubled per failed attempt, matching the reference's 0.3s base. */
	static constexpr float InitialRetryDelaySeconds = 0.3f;

	/**
	 * Mirrors NetworkClient.ShouldRetryFeatureFlagsRequest: HTTP 502/504 and the transient
	 * status-zero connection failures retry; every other protocol status, connection failure, and
	 * data-processing failure is terminal.
	 */
	inline bool ShouldRetry(EPostHogFeatureFlagFailureReason Reason, int32 StatusCode)
	{
		switch (Reason)
		{
		case EPostHogFeatureFlagFailureReason::Protocol:
			return StatusCode == 502 || StatusCode == 504;
		case EPostHogFeatureFlagFailureReason::Timeout:
		case EPostHogFeatureFlagFailureReason::ConnectionLost:
			return StatusCode == 0;
		default:
			return false;
		}
	}

	/** Deterministic exponential backoff: 300 ms, 600 ms, 1200 ms, ... for failed attempts 1, 2, 3. */
	inline float GetRetryDelaySeconds(int32 FailedAttempt)
	{
		// Bounded shift so an extreme configured retry count cannot overflow the multiplier.
		const int32 Shift = FMath::Clamp(FailedAttempt - 1, 0, 20);
		return InitialRetryDelaySeconds * static_cast<float>(1 << Shift);
	}
}
