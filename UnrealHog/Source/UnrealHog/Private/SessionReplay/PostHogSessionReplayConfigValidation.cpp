#include "SessionReplay/PostHogSessionReplayConfigValidation.h"

#include "Math/UnrealMathUtility.h"

namespace
{
	bool IsSupportedLogLevel(EPostHogSessionReplayLogLevel Level)
	{
		switch (Level)
		{
		case EPostHogSessionReplayLogLevel::Log:
		case EPostHogSessionReplayLogLevel::Warning:
		case EPostHogSessionReplayLogLevel::Error:
			return true;
		default:
			return false;
		}
	}
}

bool PostHogSessionReplayConfigValidation::TryValidate(const FPostHogSessionReplayConfig& Config, FPostHogValidatedSessionReplayConfig& OutValidated, FString& OutFailureReason)
{
	if (!FMath::IsFinite(Config.ThrottleDelaySeconds) || Config.ThrottleDelaySeconds < MinThrottleDelaySeconds)
	{
		OutFailureReason = FString::Printf(TEXT("ThrottleDelaySeconds must be a finite value of at least %g seconds."), MinThrottleDelaySeconds);
		return false;
	}

	if (Config.ScreenshotQuality < MinScreenshotQuality || Config.ScreenshotQuality > MaxScreenshotQuality)
	{
		OutFailureReason = FString::Printf(TEXT("ScreenshotQuality must be between %d and %d."), MinScreenshotQuality, MaxScreenshotQuality);
		return false;
	}

	if (!FMath::IsFinite(Config.ScreenshotScale) || Config.ScreenshotScale < MinScreenshotScale || Config.ScreenshotScale > MaxScreenshotScale)
	{
		OutFailureReason = FString::Printf(TEXT("ScreenshotScale must be a finite value between %g and %g."), MinScreenshotScale, MaxScreenshotScale);
		return false;
	}

	if (!IsSupportedLogLevel(Config.MinLogLevel))
	{
		OutFailureReason = TEXT("MinLogLevel must be one of Log, Warning, or Error.");
		return false;
	}

	if (Config.FlushEventCount < 1)
	{
		OutFailureReason = TEXT("FlushEventCount must be at least 1.");
		return false;
	}

	if (Config.FlushIntervalSeconds < 1)
	{
		OutFailureReason = TEXT("FlushIntervalSeconds must be at least 1.");
		return false;
	}

	if (Config.MaxQueueSize < 1)
	{
		OutFailureReason = TEXT("MaxQueueSize must be at least 1.");
		return false;
	}

	OutValidated.ThrottleDelaySeconds = Config.ThrottleDelaySeconds;
	OutValidated.ScreenshotQuality = Config.ScreenshotQuality;
	OutValidated.ScreenshotScale = Config.ScreenshotScale;
	OutValidated.bCaptureNetworkTelemetry = Config.bCaptureNetworkTelemetry;
	OutValidated.bCaptureLogs = Config.bCaptureLogs;
	OutValidated.MinLogLevel = Config.MinLogLevel;
	OutValidated.FlushEventCount = Config.FlushEventCount;
	OutValidated.FlushIntervalSeconds = Config.FlushIntervalSeconds;
	OutValidated.MaxQueueSize = Config.MaxQueueSize;

	OutFailureReason.Reset();
	return true;
}
