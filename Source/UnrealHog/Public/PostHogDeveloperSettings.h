// Trevor Eckhoff, 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Logging/PostHogLogger.h"
#include "PostHogDeveloperSettings.generated.h"

/**
 * @enum EPostHogHost
 * @brief Enumerates the possible hosting options for PostHog analytics events.
 *
 * This enum defines the available host configurations for sending analytics data
 * with the PostHog SDK, allowing users to target specific regional servers or a custom host.
 *
 * - US: Send events to the United States PostHog instance. (https://us.i.posthog.com)
 * - EU: Send events to the European Union PostHog instance. (https://eu.i.posthog.com)
 * - Custom: Use a custom host URL specified in the project settings.
 */
UENUM(BlueprintType)
enum class EPostHogHost : uint8
{
	US,
	EU,
	Custom
};

/**
 * @enum EPostHogPersonProfiles
 * @brief Specifies the behavior for creating or updating person profiles in PostHog analytics.
 *
 * This enum defines the conditions under which person profiles are created or updated in PostHog:
 *
 * - Always: Always create or update person profiles irrespective of user identification status.
 * - IdentifiedOnly: Create or update person profiles only when the user is identified.
 * - Never: Never create or update person profiles.
 */
UENUM(BlueprintType)
enum class EPostHogPersonProfiles : uint8
{
	Always,
	IdentifiedOnly UMETA(DisplayName = "Identified Only"),
	Never
};

/**
 * @enum EPostHogSessionReplayLogLevel
 * @brief Defines levels of verbosity for Unreal log messages included in session replay.
 *
 * This enum specifies the minimum log severity to capture when the session replay
 * log capture feature is enabled. It provides options to filter log messages based
 * on their importance or severity level.
 *
 * - Log: Captures all log messages, including the least severe ones.
 * - Warning: Captures log messages of warning level and above.
 * - Error: Captures only error-level log messages.
 */
UENUM(BlueprintType)
enum class EPostHogSessionReplayLogLevel : uint8
{
	Log,
	Warning,
	Error
};

/**
 * @struct FPostHogSessionReplayConfig
 * @brief Configuration for PostHog session replay features in Unreal Engine projects.
 *
 * This structure provides multiple settings to fine-tune the behavior of session replay functionality,
 * including screenshot captures, telemetry, logging, and event flushing. It allows developers to
 * customize how session replay data is collected, compressed, and transmitted for analysis.
 *
 * Designed to be configured through the Unreal Engine editor or programmatically.
 */
USTRUCT(BlueprintType)
struct UNREALHOG_API FPostHogSessionReplayConfig
{
	GENERATED_BODY()

	// Minimum time between session replay screenshot captures, in seconds.
	UPROPERTY(EditAnywhere, Category="PostHog|Session Replay", meta = (ClampMin = "0.1", UIMin = "0.1"))
	float ThrottleDelaySeconds = 1.0f;

	// JPEG compression quality for session replay screenshots.
	UPROPERTY(EditAnywhere, Category="PostHog|Session Replay", meta = (ClampMin = 1, ClampMax = 100, UIMin = 1, UIMax = 100))
	int32 ScreenshotQuality = 80;

	// Whether to include HTTP request telemetry in session replay.
	UPROPERTY(EditAnywhere, Category="PostHog|Session Replay")
	bool bCaptureNetworkTelemetry = true;

	// Whether to include Unreal log messages in session replay.
	UPROPERTY(EditAnywhere, Category="PostHog|Session Replay")
	bool bCaptureLogs = false;

	// Minimum log verbosity to include when session replay log capture is enabled.
	UPROPERTY(EditAnywhere, Category="PostHog|Session Replay", meta = (EditCondition = "bCaptureLogs"))
	EPostHogSessionReplayLogLevel MinLogLevel = EPostHogSessionReplayLogLevel::Error;

	// Scale factor applied to session replay screenshots before upload.
	UPROPERTY(EditAnywhere, Category="PostHog|Session Replay", meta = (ClampMin = "0.1", ClampMax = "1.0", UIMin = "0.1", UIMax = "1.0"))
	float ScreenshotScale = 0.75f;

	// Number of session replay events able to be queued before triggering an automatic flush.
	UPROPERTY(EditAnywhere, Category="PostHog|Session Replay", meta = (ClampMin = 1, UIMin = 1))
	int32 FlushEventCount = 20;

	// How often to attempt to automatically flush session replay events, in seconds.
	UPROPERTY(EditAnywhere, Category="PostHog|Session Replay", meta = (ClampMin = 1, UIMin = 1))
	int32 FlushIntervalSeconds = 30;

	// Maximum number of session replay events able to be stored in the queue.
	UPROPERTY(EditAnywhere, Category="PostHog|Session Replay", meta = (ClampMin = 1, UIMin = 1))
	int32 MaxQueueSize = 100;
};

/**
 * @class UPostHogDeveloperSettings
 * @brief Provides project-wide configuration settings for integrating PostHog analytics into Unreal Engine projects.
 *
 * This class allows developers to configure various aspects of the PostHog SDK, such as event delivery settings,
 * logging levels, feature flag behavior, and exception tracking. Settings can be adjusted through the Unreal Editor
 * under the "Project Settings" menu.
 *
 * Inherits from UDeveloperSettings to allow engine and project-level customization.
 */
UCLASS(Config=Analytics, DefaultConfig, meta = (DisplayName = "PostHog"))
class UNREALHOG_API UPostHogDeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()
	
public:
	UPostHogDeveloperSettings(const FObjectInitializer& ObjectInitializer);
	
	UFUNCTION()
	FString GetResolvedHost() const;
	
	UFUNCTION()
	bool IsAnalyticsEnabled() const;
	
#if WITH_EDITOR
	virtual bool CanEditChange(const FProperty* InProperty) const override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	
private:
	FString HostUS = "https://us.i.posthog.com";
	FString HostEU = "https://eu.i.posthog.com";
	
protected:
	// Whether analytics collection is enabled for this project.
	UPROPERTY(Config, EditAnywhere, Category="PostHog")
	bool bAnalyticsEnabled = true;
	
	// The public project API key from PostHog. Should start with "phc_". May be soft-validated in the future.
	UPROPERTY(Config, EditAnywhere, Category="PostHog", meta = (DisplayName = "Project Public API Key"))
	FString ApiKey = "";
	
	// Which host to use for sending analytics events.
	UPROPERTY(Config, EditAnywhere, Category="PostHog|Host")
	EPostHogHost HostType = EPostHogHost::US;
	
	// The URL for the host instance. To modify yourself, set HostType to Custom.
	UPROPERTY(Config, EditAnywhere, Category="PostHog|Host", meta=(EditCondition="HostType == EPostHogHost::Custom", DisplayAfter="HostType"))
	FString Host = HostUS;
	
	// Number of events able to be queued before triggering an automatic flush.
	UPROPERTY(Config, EditAnywhere, Category="PostHog|Event Delivery", meta = (ClampMin = 1, UIMin = 1))
	int32 FlushEventCount = 20;
	
	// How often to attempt to automatically flush events to the server, in seconds.
	UPROPERTY(Config, EditAnywhere, Category="PostHog|Event Delivery", meta = (ClampMin = 1, UIMin = 1))
	int32 FlushIntervalSeconds = 30;
	
	// Maximum number of events able to be stored in the queue.
	// If exceeded, the oldest events will be dropped.
	UPROPERTY(Config, EditAnywhere, Category="PostHog|Event Delivery", meta = (ClampMin = 1, UIMin = 1))
	int32 MaxQueueSize = 1000;
	
	// Maximum number of events able to be sent in a single batch when the queue is flushed.
	UPROPERTY(Config, EditAnywhere, Category="PostHog|Event Delivery", meta = (ClampMin = 1, UIMin = 1))
	int32 MaxBatchSize = 50;

	// Whether to automatically capture application lifecycle events.
	// Application lifecycle events include installation, updated, opened, and set inactive (or put in background).
	UPROPERTY(Config, EditAnywhere, Category="PostHog|Lifecycle")
	bool bCaptureApplicationLifecycleEvents = true;

	// Controls when PostHog should create or update person profiles.
	UPROPERTY(Config, EditAnywhere, Category="PostHog|User Profiles")
	EPostHogPersonProfiles PersonProfiles = EPostHogPersonProfiles::IdentifiedOnly;

	// Minimum log level for SDK logging.
	UPROPERTY(Config, EditAnywhere, Category="PostHog|Logging")
	EPostHogLogLevel LogLevel = EPostHogLogLevel::Warning;

	// Whether to reuse the anonymous ID across reset calls.
	UPROPERTY(Config, EditAnywhere, Category="PostHog|Identity")
	bool bReuseAnonymousId = false;

	// Whether to fetch feature flags during SDK initialization.
	UPROPERTY(Config, EditAnywhere, Category="PostHog|Feature Flags")
	bool bPreloadFeatureFlags = true;

	// Maximum number of retries for feature flag requests after transient network errors.
	UPROPERTY(Config, EditAnywhere, Category="PostHog|Feature Flags", meta = (ClampMin = 0, UIMin = 0))
	int32 FeatureFlagRequestMaxRetries = 1;

	// Whether to send $feature_flag_called events when feature flags are accessed.
	UPROPERTY(Config, EditAnywhere, Category="PostHog|Feature Flags")
	bool bSendFeatureFlagEvent = true;

	// Whether to include default device and app properties in feature flag requests.
	UPROPERTY(Config, EditAnywhere, Category="PostHog|Feature Flags")
	bool bSendDefaultPersonPropertiesForFlags = true;

	// Whether to flush queued events before the application quits.
	UPROPERTY(Config, EditAnywhere, Category="PostHog|Shutdown")
	bool bFlushOnQuit = true;

	// Maximum time to wait for the final flush on quit, in seconds.
	UPROPERTY(Config, EditAnywhere, Category="PostHog|Shutdown", meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "bFlushOnQuit"))
	float FlushOnQuitTimeoutSeconds = 3.0f;

	// Whether to automatically capture unhandled exceptions.
	UPROPERTY(Config, EditAnywhere, Category="PostHog|Exception Tracking")
	bool bCaptureExceptions = true;

	// Minimum time between captured exceptions, in milliseconds. Set to 0 to disable debouncing.
	UPROPERTY(Config, EditAnywhere, Category="PostHog|Exception Tracking", meta = (ClampMin = 0, UIMin = 0, EditCondition = "bCaptureExceptions"))
	int32 ExceptionDebounceIntervalMs = 1000;

	// Whether exception capture should also run in the Unreal Editor.
	UPROPERTY(Config, EditAnywhere, Category="PostHog|Exception Tracking", meta = (EditCondition = "bCaptureExceptions"))
	bool bCaptureExceptionsInEditor = true;

	// Whether to enable session replay capture.
	UPROPERTY(Config, EditAnywhere, Category="PostHog|Session Replay")
	bool bSessionReplay = false;

	// Configuration used when session replay is enabled.
	UPROPERTY(Config, EditAnywhere, Category="PostHog|Session Replay", meta = (EditCondition = "bSessionReplay"))
	FPostHogSessionReplayConfig SessionReplayConfig;
};
