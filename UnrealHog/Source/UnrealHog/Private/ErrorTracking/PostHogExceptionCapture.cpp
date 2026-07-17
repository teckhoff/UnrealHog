#include "ErrorTracking/PostHogExceptionCapture.h"

#include "Consent/PostHogConsentController.h"
#include "CoreGlobals.h"
#include "Events/PostHogExceptionInput.h"
#include "Misc/CoreDelegates.h"
#include "Math/NumericLimits.h"

FPostHogExceptionCapture::FPostHogExceptionCapture(FPostHogConsentController& InConsentController, FMonotonicClock InClock)
	: ConsentController(InConsentController)
	, Clock(MoveTemp(InClock))
	, LastCaptureSeconds(-TNumericLimits<double>::Max())
{
}

void FPostHogExceptionCapture::RegisterHandlers(bool bAllowInEditor, int32 InDebounceIntervalMs)
{
	if (EnsureFailedHandle.IsValid())
	{
		return;
	}

	if (!bAllowInEditor && GIsEditor)
	{
		return;
	}

	DebounceIntervalMs = InDebounceIntervalMs;
	EnsureFailedHandle = FCoreDelegates::OnEnsureFailed.AddRaw(this, &FPostHogExceptionCapture::HandleEnsureFailed);
}

void FPostHogExceptionCapture::UnregisterHandlers()
{
	if (!EnsureFailedHandle.IsValid())
	{
		return;
	}

	FCoreDelegates::OnEnsureFailed.Remove(EnsureFailedHandle);
	EnsureFailedHandle.Reset();
}

bool FPostHogExceptionCapture::IsRegistered() const
{
	return EnsureFailedHandle.IsValid();
}

void FPostHogExceptionCapture::SimulateEnsureFailed(const ANSICHAR* Expr, const ANSICHAR* File, int32 Line, const TCHAR* Msg, const TCHAR* CombinedErrorMsg)
{
	HandleEnsureFailed(Expr, File, Line, Msg, CombinedErrorMsg);
}

void FPostHogExceptionCapture::HandleEnsureFailed(const ANSICHAR* Expr, const ANSICHAR* File, int32 Line, const TCHAR* Msg, const TCHAR* CombinedErrorMsg)
{
	FPostHogExceptionInput Input;
	Input.Message = CombinedErrorMsg;
	Input.Type = TEXT("Ensure");
	Input.StackTrace = FString::Printf(TEXT("%hs:%d"), File, Line);
	Input.bHandled = true;

	CaptureAdapted(MoveTemp(Input));
}

void FPostHogExceptionCapture::CaptureAdapted(FPostHogExceptionInput&& Input)
{
	// Guards against recursion if UnrealHog's own logging/ensures fire while a capture is in
	// flight (e.g. a failed ensure inside ConsentController.CaptureException's call chain).
	if (bIsCapturingException)
	{
		return;
	}

	const double Now = Clock();
	if ((Now - LastCaptureSeconds) * 1000.0 < DebounceIntervalMs)
	{
		return;
	}

	LastCaptureSeconds = Now;

	bIsCapturingException = true;
	ConsentController.CaptureException(Input, nullptr);
	bIsCapturingException = false;
}
