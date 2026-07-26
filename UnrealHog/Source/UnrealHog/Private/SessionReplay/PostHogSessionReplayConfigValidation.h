#pragma once

#include "CoreMinimal.h"
#include "PostHogDeveloperSettings.h"

/**
 * @brief A session-replay configuration snapshot that has passed runtime validation.
 *
 * Only produced by PostHogSessionReplayConfigValidation::TryValidate. Holding one of these is the
 * proof that every replay value is inside its supported range, so later replay collaborators
 * (SDKP-014 onward) never have to re-check bounds or invent fallbacks.
 */
struct FPostHogValidatedSessionReplayConfig
{
	float ThrottleDelaySeconds = 1.0f;
	int32 ScreenshotQuality = 80;
	float ScreenshotScale = 0.75f;
	bool bCaptureNetworkTelemetry = true;
	bool bCaptureLogs = false;
	EPostHogSessionReplayLogLevel MinLogLevel = EPostHogSessionReplayLogLevel::Error;
	int32 FlushEventCount = 20;
	int32 FlushIntervalSeconds = 30;
	int32 MaxQueueSize = 100;
};

namespace PostHogSessionReplayConfigValidation
{
	// Minimum time between replay screenshot captures, in seconds. Matches Unity's
	// PostHogSessionReplayConfig.Validate.
	constexpr float MinThrottleDelaySeconds = 0.1f;

	constexpr int32 MinScreenshotQuality = 1;
	constexpr int32 MaxScreenshotQuality = 100;

	constexpr float MinScreenshotScale = 0.1f;
	constexpr float MaxScreenshotScale = 1.0f;

	/**
	 * Validates a serialized replay configuration without creating any capture, queue, or HTTP object.
	 *
	 * The editor clamps on FPostHogSessionReplayConfig are advisory only: config files, older
	 * projects, and reflection-driven writes can all deliver out-of-range or non-finite values, so
	 * this is the runtime boundary that decides whether replay may run at all.
	 *
	 * @return true and populates OutValidated on success; false with an actionable OutFailureReason otherwise.
	 */
	bool TryValidate(const FPostHogSessionReplayConfig& Config, FPostHogValidatedSessionReplayConfig& OutValidated, FString& OutFailureReason);
}
