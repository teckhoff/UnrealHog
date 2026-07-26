#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Http.h"

/**
 * @brief Minimal in-memory IHttpResponse used by FPostHogFakeHttpRequest::SimulateComplete to
 * hand a deterministic response back to the request's completion delegate.
 */
class FPostHogFakeHttpResponse final : public IHttpResponse
{
public:
	FPostHogFakeHttpResponse(int32 InResponseCode, const FString& InContent)
		: ResponseCode(InResponseCode)
	{
		const FTCHARToUTF8 Converter(*InContent);
		Content.Append(reinterpret_cast<const uint8*>(Converter.Get()), Converter.Length());
	}

	// IHttpBase
	virtual const FString& GetURL() const override { return Url; }
	virtual const FString& GetEffectiveURL() const override { return Url; }
	virtual EHttpRequestStatus::Type GetStatus() const override { return EHttpRequestStatus::Succeeded; }
	virtual EHttpFailureReason GetFailureReason() const override { return EHttpFailureReason::None; }
	virtual FString GetURLParameter(const FString&) const override { return FString(); }
	virtual FString GetHeader(const FString&) const override { return FString(); }
	virtual TArray<FString> GetAllHeaders() const override { return TArray<FString>(); }
	virtual FString GetContentType() const override { return TEXT("application/json"); }
	virtual uint64 GetContentLength() const override { return static_cast<uint64>(Content.Num()); }
	virtual const TArray<uint8>& GetContent() const override { return Content; }

	// IHttpResponse
	virtual TArray<uint8> TakeContent() override { return Content; }
	virtual int32 GetResponseCode() const override { return ResponseCode; }
	virtual FString GetContentAsString() const override
	{
		return FString(FUTF8ToTCHAR(reinterpret_cast<const ANSICHAR*>(Content.GetData()), Content.Num()));
	}
	virtual FUtf8StringView GetContentAsUtf8StringView() const override { return FUtf8StringView(reinterpret_cast<const UTF8CHAR*>(Content.GetData()), Content.Num()); }

private:
	int32 ResponseCode;
	FString Url;
	TArray<uint8> Content;
};

/**
 * @brief Deterministic fake IHttpRequest for Automation tests, letting FPostHogHttpClient tests
 * observe request construction and drive completion without FHttpModule or a real socket.
 *
 * ProcessRequest() only reports the configured start result; it never auto-invokes the
 * completion delegate. Tests call SimulateComplete() explicitly, including after a forced
 * start failure, to prove a late platform callback cannot double-complete the queue.
 */
class FPostHogFakeHttpRequest final : public IHttpRequest
{
public:
	// IHttpBase
	virtual const FString& GetURL() const override { return Url; }
	virtual const FString& GetEffectiveURL() const override { return Url; }
	virtual EHttpRequestStatus::Type GetStatus() const override { return Status; }
	virtual EHttpFailureReason GetFailureReason() const override { return FailureReason; }
	virtual FString GetURLParameter(const FString&) const override { return FString(); }
	virtual FString GetHeader(const FString& HeaderName) const override
	{
		if (const FString* Value = Headers.Find(HeaderName))
		{
			return *Value;
		}
		return FString();
	}
	virtual TArray<FString> GetAllHeaders() const override
	{
		TArray<FString> Result;
		for (const TPair<FString, FString>& Header : Headers)
		{
			Result.Add(FString::Printf(TEXT("%s: %s"), *Header.Key, *Header.Value));
		}
		return Result;
	}
	virtual FString GetContentType() const override { return GetHeader(TEXT("Content-Type")); }
	virtual uint64 GetContentLength() const override { return static_cast<uint64>(ContentBytes.Num()); }
	virtual const TArray<uint8>& GetContent() const override { return ContentBytes; }

	// IHttpRequest
	virtual FString GetVerb() const override { return Verb; }
	virtual void SetVerb(const FString& InVerb) override { Verb = InVerb; }
	virtual void SetURL(const FString& InUrl) override { Url = InUrl; }
	virtual FString GetOption(const FName) const override { return FString(); }
	virtual void SetOption(const FName, const FString&) override {}
	virtual void SetContent(const TArray<uint8>& InContent) override { ContentBytes = InContent; }
	virtual void SetContent(TArray<uint8>&& InContent) override { ContentBytes = MoveTemp(InContent); }
	virtual void SetContentAsString(const FString& InContentString) override
	{
		ContentString = InContentString;
		FTCHARToUTF8 Converter(*InContentString);
		ContentBytes.SetNum(Converter.Length());
		FMemory::Memcpy(ContentBytes.GetData(), Converter.Get(), Converter.Length());
	}
	virtual bool SetContentAsStreamedFile(const FString&) override { return false; }
	virtual bool SetContentFromStream(TSharedRef<FArchive, ESPMode::ThreadSafe>) override { return false; }
	virtual bool SetResponseBodyReceiveStream(TSharedRef<FArchive>) override { return false; }
	virtual void SetHeader(const FString& HeaderName, const FString& HeaderValue) override { Headers.Add(HeaderName, HeaderValue); }
	virtual void AppendToHeader(const FString& HeaderName, const FString& AdditionalHeaderValue) override
	{
		if (FString* Existing = Headers.Find(HeaderName))
		{
			*Existing = *Existing + TEXT(", ") + AdditionalHeaderValue;
		}
		else
		{
			Headers.Add(HeaderName, AdditionalHeaderValue);
		}
	}
	virtual void SetTimeout(float InTimeoutSecs) override { TimeoutSecs = InTimeoutSecs; }
	virtual void SetActivityTimeout(float) override {}
	virtual void ClearTimeout() override { TimeoutSecs.Reset(); }
	virtual void ResetTimeoutStatus() override {}
	virtual TOptional<float> GetTimeout() const override { return TimeoutSecs; }

	virtual bool ProcessRequest() override
	{
		bProcessRequestCalled = true;
		Status = bNextStartResult ? EHttpRequestStatus::Processing : EHttpRequestStatus::Failed;
		return bNextStartResult;
	}

	virtual FHttpRequestCompleteDelegate& OnProcessRequestComplete() override { return CompleteDelegate; }
	virtual FHttpRequestProgressDelegate64& OnRequestProgress64() override { return ProgressDelegate; }
	virtual FHttpRequestWillRetryDelegate& OnRequestWillRetry() override { return WillRetryDelegate; }
	virtual FHttpRequestHeaderReceivedDelegate& OnHeaderReceived() override { return HeaderReceivedDelegate; }
	virtual FHttpRequestStatusCodeReceivedDelegate& OnStatusCodeReceived() override { return StatusCodeReceivedDelegate; }

	virtual void CancelRequest() override
	{
		bCancelled = true;
		CompleteDelegate.Unbind();
	}

	virtual const FHttpResponsePtr GetResponse() const override { return LastResponse; }
	virtual void Tick(float) override {}
	virtual float GetElapsedTime() const override { return ElapsedSeconds; }
	virtual void SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy InThreadPolicy) override { ThreadPolicy = InThreadPolicy; }
	virtual EHttpRequestDelegateThreadPolicy GetDelegateThreadPolicy() const override { return ThreadPolicy; }
	virtual void ProcessRequestUntilComplete() override {}
	virtual void SetPriority(EHttpRequestPriority InPriority) override { Priority = InPriority; }
	virtual EHttpRequestPriority GetPriority() const override { return Priority; }

	// Test control surface.
	void SetStartResult(bool bInNextStartResult) { bNextStartResult = bInNextStartResult; }

	// Delivers a completion callback as the platform HTTP backend would; safe to call even
	// after ProcessRequest() returned false or the request was cancelled, since the delegate
	// is only bound while neither of those has happened (see CancelRequest, and callers must
	// not rely on this to skip forced-start-failure late-callback simulation — that path stays
	// bound intentionally so tests can prove the completion guard, not the delegate binding, is
	// what prevents a double-fire).
	void SimulateComplete(bool bSuccess, int32 StatusCode, const FString& Body)
	{
		Status = bSuccess ? EHttpRequestStatus::Succeeded : EHttpRequestStatus::Failed;
		LastResponse = MakeShared<FPostHogFakeHttpResponse, ESPMode::ThreadSafe>(StatusCode, Body);
		CompleteDelegate.ExecuteIfBound(AsShared(), LastResponse, bSuccess);
	}

	/**
	 * Reports received bytes exactly as a platform backend does once the peer starts answering, so
	 * tests can distinguish "the server began responding" from "a response object exists", which the
	 * Apple backend creates before any network I/O.
	 */
	void SimulateReceivedBytes(uint64 BytesReceived)
	{
		ProgressDelegate.ExecuteIfBound(AsShared(), 0, BytesReceived);
	}

	/**
	 * Reports sent bytes exactly as a platform backend does once request bytes reach the wire, which
	 * only happens after the connection and any TLS handshake succeeded.
	 */
	void SimulateSentBytes(uint64 BytesSent)
	{
		ProgressDelegate.ExecuteIfBound(AsShared(), BytesSent, 0);
	}

	/** Reports a received status line, as the engine does when the peer's response headers arrive. */
	void SimulateStatusCodeReceived(int32 StatusCode)
	{
		StatusCodeReceivedDelegate.ExecuteIfBound(AsShared(), StatusCode);
	}

	/**
	 * Delivers a transport-level failure exactly as the engine does: no success flag, the platform's
	 * coarse EHttpFailureReason, either no response at all (nothing ever arrived) or a partial
	 * response carrying the status the server had already sent before the connection dropped, and the
	 * time the attempt ran before failing.
	 *
	 * A response object alone says nothing about whether the server answered; call
	 * SimulateReceivedBytes/SimulateStatusCodeReceived first to model a peer that actually did.
	 */
	void SimulateFailure(EHttpFailureReason InFailureReason,
		TOptional<int32> PartialResponseStatusCode = TOptional<int32>(),
		float InElapsedSeconds = 0.0f)
	{
		Status = EHttpRequestStatus::Failed;
		FailureReason = InFailureReason;
		ElapsedSeconds = InElapsedSeconds;
		LastResponse = PartialResponseStatusCode.IsSet()
			? MakeShared<FPostHogFakeHttpResponse, ESPMode::ThreadSafe>(PartialResponseStatusCode.GetValue(), FString())
			: FHttpResponsePtr();
		CompleteDelegate.ExecuteIfBound(AsShared(), LastResponse, false);
	}

	FString Verb;
	FString Url;
	EHttpFailureReason FailureReason = EHttpFailureReason::None;
	TMap<FString, FString> Headers;
	FString ContentString;
	TArray<uint8> ContentBytes;
	TOptional<float> TimeoutSecs;
	float ElapsedSeconds = 0.0f;
	bool bCancelled = false;
	bool bProcessRequestCalled = false;
	bool bNextStartResult = true;

private:
	EHttpRequestStatus::Type Status = EHttpRequestStatus::NotStarted;
	FHttpResponsePtr LastResponse;
	EHttpRequestDelegateThreadPolicy ThreadPolicy = EHttpRequestDelegateThreadPolicy::CompleteOnGameThread;
	EHttpRequestPriority Priority = EHttpRequestPriority::Normal;

	FHttpRequestCompleteDelegate CompleteDelegate;
	FHttpRequestProgressDelegate64 ProgressDelegate;
	FHttpRequestWillRetryDelegate WillRetryDelegate;
	FHttpRequestHeaderReceivedDelegate HeaderReceivedDelegate;
	FHttpRequestStatusCodeReceivedDelegate StatusCodeReceivedDelegate;
};

/**
 * @brief Injectable request factory that hands out FPostHogFakeHttpRequest instances, letting
 * tests configure the next request's start result and inspect what FPostHogHttpClient built.
 */
class FPostHogFakeHttpRequestFactory
{
public:
	FHttpRequestRef operator()()
	{
		const TSharedRef<FPostHogFakeHttpRequest, ESPMode::ThreadSafe> NewRequest = MakeShared<FPostHogFakeHttpRequest, ESPMode::ThreadSafe>();
		NewRequest->SetStartResult(bNextStartResult);
		CreatedRequests.Add(NewRequest);
		return NewRequest;
	}

	FPostHogFakeHttpRequest& Last() const
	{
		check(CreatedRequests.Num() > 0);
		return *CreatedRequests.Last();
	}

	TArray<TSharedRef<FPostHogFakeHttpRequest, ESPMode::ThreadSafe>> CreatedRequests;
	bool bNextStartResult = true;
};

#endif // WITH_DEV_AUTOMATION_TESTS
