#pragma once

#include "CoreMinimal.h"
#include "Events/PostHogEventQueue.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"

/**
 * @brief Bounded drain-then-exit state machine for EP-027 flush-on-quit.
 *
 * Owns no engine lifetime concerns itself: all side effects (starting a flush, cancelling and
 * draining storage, scheduling/cancelling a timeout, and requesting engine exit) are injected as
 * TFunctions so the state machine can be exercised deterministically in Automation tests without
 * a real FTSTicker or FCoreDelegates wiring. Production callers wire this to
 * FPostHogConsentController::RequestFlush/Shutdown and FTSTicker::GetCoreTicker().
 *
 * BeginFlushAndQuit() is safe to call more than once (e.g. the window-close veto and an explicit
 * FlushAndQuit() API call racing): only the first call starts a flush and schedules a timeout,
 * and FinalizeQuit() guarantees ShutdownFunc/RequestExitFunc each run exactly once, whichever of
 * flush-completion or timeout reaches it first. A flush completion that arrives after
 * finalization (e.g. racing the timeout) is a no-op.
 */
class FPostHogQuitFlushCoordinator
{
public:
	using FRequestFlushFunc = TFunction<void(FPostHogEventQueueFlushComplete)>;
	using FShutdownFunc = TFunction<void()>;
	using FRequestExitFunc = TFunction<void()>;
	// Schedules OnTimeout to run after DelaySeconds and returns a cancel closure the coordinator
	// invokes if the timeout should be stopped early (flush completed first).
	using FScheduleTimeoutFunc = TFunction<TFunction<void()>(float DelaySeconds, TFunction<void()> OnTimeout)>;

	FPostHogQuitFlushCoordinator(FRequestFlushFunc InRequestFlushFunc,
		FShutdownFunc InShutdownFunc,
		float InTimeoutSeconds,
		FRequestExitFunc InRequestExitFunc = nullptr,
		FScheduleTimeoutFunc InScheduleTimeoutFunc = nullptr);
	~FPostHogQuitFlushCoordinator();

	// No-op if a flush-and-quit is already in progress or has already finalized. Otherwise
	// schedules the timeout, then requests the flush; finalization runs on whichever completes
	// first.
	void BeginFlushAndQuit();

	bool IsFinalized() const { return bFinalized; }

private:
	void FinalizeQuit();

	static TFunction<void()> ScheduleTimeoutWithTicker(float DelaySeconds, TFunction<void()> OnTimeout);

	FRequestFlushFunc RequestFlushFunc;
	FShutdownFunc ShutdownFunc;
	FRequestExitFunc RequestExitFunc;
	FScheduleTimeoutFunc ScheduleTimeoutFunc;
	float TimeoutSeconds;

	TFunction<void()> CancelTimeout;
	bool bInProgress = false;
	bool bFinalized = false;

	// EventQueue can retain the RequestFlushFunc completion (and, for the fake test scheduler, the
	// timeout callback) past this object's lifetime -- e.g. Deinitialize destroys the coordinator
	// while a flush it started is still in flight. Both callbacks capture this flag by shared
	// ownership and check it before touching `this`, so a callback arriving after destruction
	// no-ops instead of dereferencing freed memory.
	TSharedRef<bool> LivenessFlag = MakeShared<bool>(true);
};
