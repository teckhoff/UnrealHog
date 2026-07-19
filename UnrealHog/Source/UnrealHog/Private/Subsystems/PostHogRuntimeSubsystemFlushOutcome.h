#pragma once

#include "Events/PostHogEventQueue.h"
#include "Subsystems/PostHogRuntimeSubsystem.h"

inline EPostHogFlushOutcome TranslateFlushOutcome(EPostHogEventQueueFlushResult Result)
{
	switch (Result)
	{
	case EPostHogEventQueueFlushResult::Drained:
		return EPostHogFlushOutcome::Drained;
	case EPostHogEventQueueFlushResult::Failed:
		return EPostHogFlushOutcome::Failed;
	case EPostHogEventQueueFlushResult::Cancelled:
		return EPostHogFlushOutcome::Cancelled;
	case EPostHogEventQueueFlushResult::ProgressBlocked:
		return EPostHogFlushOutcome::ProgressBlocked;
	case EPostHogEventQueueFlushResult::Paused:
		return EPostHogFlushOutcome::Paused;
	case EPostHogEventQueueFlushResult::SkippedOffline:
		return EPostHogFlushOutcome::SkippedOffline;
	case EPostHogEventQueueFlushResult::Empty:
	default:
		return EPostHogFlushOutcome::Empty;
	}
}
