#include "Lifecycle/PostHogQuitFlushCoordinator.h"

#include "Containers/Ticker.h"
#include "CoreGlobals.h"

FPostHogQuitFlushCoordinator::FPostHogQuitFlushCoordinator(FRequestFlushFunc InRequestFlushFunc,
	FShutdownFunc InShutdownFunc,
	float InTimeoutSeconds,
	FRequestExitFunc InRequestExitFunc,
	FScheduleTimeoutFunc InScheduleTimeoutFunc) :
	RequestFlushFunc(MoveTemp(InRequestFlushFunc)),
	ShutdownFunc(MoveTemp(InShutdownFunc)),
	RequestExitFunc(InRequestExitFunc ? MoveTemp(InRequestExitFunc) : FRequestExitFunc([]()
	{
		RequestEngineExit(TEXT("PostHog flush-and-quit"));
	})),
	ScheduleTimeoutFunc(InScheduleTimeoutFunc ? MoveTemp(InScheduleTimeoutFunc) : FScheduleTimeoutFunc(&FPostHogQuitFlushCoordinator::ScheduleTimeoutWithTicker)),
	TimeoutSeconds(InTimeoutSeconds)
{
}

FPostHogQuitFlushCoordinator::~FPostHogQuitFlushCoordinator()
{
	*LivenessFlag = false;

	if (CancelTimeout)
	{
		CancelTimeout();
		CancelTimeout = nullptr;
	}
}

void FPostHogQuitFlushCoordinator::BeginFlushAndQuit()
{
	if (bInProgress || bFinalized)
	{
		return;
	}

	bInProgress = true;

	TSharedRef<bool> Liveness = LivenessFlag;

	CancelTimeout = ScheduleTimeoutFunc(TimeoutSeconds, [this, Liveness]()
	{
		if (*Liveness)
		{
			FinalizeQuit();
		}
	});

	RequestFlushFunc([this, Liveness](EPostHogEventQueueFlushResult)
	{
		if (*Liveness)
		{
			FinalizeQuit();
		}
	});
}

void FPostHogQuitFlushCoordinator::FinalizeQuit()
{
	if (bFinalized)
	{
		return;
	}

	bFinalized = true;

	if (CancelTimeout)
	{
		CancelTimeout();
		CancelTimeout = nullptr;
	}

	if (ShutdownFunc)
	{
		ShutdownFunc();
	}

	if (RequestExitFunc)
	{
		RequestExitFunc();
	}
}

TFunction<void()> FPostHogQuitFlushCoordinator::ScheduleTimeoutWithTicker(float DelaySeconds, TFunction<void()> OnTimeout)
{
	const FTSTicker::FDelegateHandle TickerHandle = FTSTicker::GetCoreTicker().AddTicker(TEXT("PostHogQuitFlushCoordinator"), DelaySeconds,
		[OnTimeout](float) -> bool
		{
			OnTimeout();
			return false;
		});

	return [TickerHandle]()
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	};
}
