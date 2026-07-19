#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformTime.h"
#include "Templates/Function.h"

class FPostHogConsentController;
struct FPostHogExceptionInput;

/**
 * @brief Adapts the narrow set of Unreal runtime signals that can safely represent an
 * uncaught/logged exception (currently only FCoreDelegates::OnEnsureFailed) into
 * FPostHogConsentController::CaptureException calls.
 *
 * Not a UObject: owned by UPostHogRuntimeSubsystem alongside FPostHogConsentController, and kept
 * free of engine subsystem lifetime concerns so it can be exercised directly in automation tests
 * via SimulateEnsureFailed rather than the real global FCoreDelegates broadcast.
 */
class FPostHogExceptionCapture
{
public:
	using FMonotonicClock = TFunction<double()>;

	explicit FPostHogExceptionCapture(FPostHogConsentController& InConsentController, FMonotonicClock InClock = &FPlatformTime::Seconds);

	// No-op if already registered (duplicate-registration guard) or if !bAllowInEditor && GIsEditor.
	void RegisterHandlers(bool bAllowInEditor, int32 InDebounceIntervalMs);

	// Idempotent; safe to call repeatedly, including when never registered.
	void UnregisterHandlers();

	bool IsRegistered() const;

	// Test seam: invokes the same handling path as the real FCoreDelegates::OnEnsureFailed
	// broadcast, without requiring the engine to actually fail an ensure.
	void SimulateEnsureFailed(const ANSICHAR* Expr, const ANSICHAR* File, int32 Line, const TCHAR* Msg, const TCHAR* CombinedErrorMsg);

private:
	void HandleEnsureFailed(const ANSICHAR* Expr, const ANSICHAR* File, int32 Line, const TCHAR* Msg, const TCHAR* CombinedErrorMsg);
	void CaptureAdapted(FPostHogExceptionInput&& Input);

	FPostHogConsentController& ConsentController;
	FMonotonicClock Clock;
	int32 DebounceIntervalMs = 0;
	double LastCaptureSeconds;
	bool bIsCapturingException = false;
	FDelegateHandle EnsureFailedHandle;
};
