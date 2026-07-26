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
 * @brief Defines levels of verbosity for future Unreal log messages included in session replay.
 *
 * This enum is serialized for future SDKP-018 session replay support and is not
 * editable until that runtime capability is implemented.
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
 * @brief Serialized compatibility settings for future PostHog session replay support.
 *
 * These properties remain serialized so existing config files keep their values, but the
 * editor disables them until SDKP-018 implements the owning runtime subsystem.
 *
 * Removal criteria: SDKP-018 must compile the replay lifecycle, capture, queue,
 * and transport path that consumes these values. Until then no runtime replay
 * collaborator is created from this struct.
 */
USTRUCT(BlueprintType)
struct UNREALHOG_API FPostHogSessionReplayConfig
{
	GENERATED_BODY()

	// Future minimum time between session replay screenshot captures, in seconds.
	UPROPERTY(EditAnywhere, Category="PostHog|Session Replay", meta = (ClampMin = "0.1", UIMin = "0.1", DisplayName = "Throttle Delay Seconds (Unavailable until SDKP-018)", ToolTip = "Unavailable until SDKP-018 implements session replay. This value remains serialized for future compatibility."))
	float ThrottleDelaySeconds = 1.0f;

	// Future JPEG compression quality for session replay screenshots.
	UPROPERTY(EditAnywhere, Category="PostHog|Session Replay", meta = (ClampMin = 1, ClampMax = 100, UIMin = 1, UIMax = 100, DisplayName = "Screenshot Quality (Unavailable until SDKP-018)", ToolTip = "Unavailable until SDKP-018 implements session replay. This value remains serialized for future compatibility."))
	int32 ScreenshotQuality = 80;

	// Future option for including HTTP request telemetry in session replay.
	UPROPERTY(EditAnywhere, Category="PostHog|Session Replay", meta = (DisplayName = "Capture Network Telemetry (Unavailable until SDKP-018)", ToolTip = "Unavailable until SDKP-018 implements session replay. This value remains serialized for future compatibility."))
	bool bCaptureNetworkTelemetry = true;

	// Future option for including Unreal log messages in session replay.
	UPROPERTY(EditAnywhere, Category="PostHog|Session Replay", meta = (DisplayName = "Capture Logs (Unavailable until SDKP-018)", ToolTip = "Unavailable until SDKP-018 implements session replay. This value remains serialized for future compatibility."))
	bool bCaptureLogs = false;

	// Future minimum log verbosity to include when session replay log capture is enabled.
	UPROPERTY(EditAnywhere, Category="PostHog|Session Replay", meta = (EditCondition = "bCaptureLogs", DisplayName = "Minimum Log Level (Unavailable until SDKP-018)", ToolTip = "Unavailable until SDKP-018 implements session replay. This value remains serialized for future compatibility."))
	EPostHogSessionReplayLogLevel MinLogLevel = EPostHogSessionReplayLogLevel::Error;

	// Future scale factor applied to session replay screenshots before upload.
	UPROPERTY(EditAnywhere, Category="PostHog|Session Replay", meta = (ClampMin = "0.1", ClampMax = "1.0", UIMin = "0.1", UIMax = "1.0", DisplayName = "Screenshot Scale (Unavailable until SDKP-018)", ToolTip = "Unavailable until SDKP-018 implements session replay. This value remains serialized for future compatibility."))
	float ScreenshotScale = 0.75f;

	// Future number of session replay events able to be queued before triggering an automatic flush.
	UPROPERTY(EditAnywhere, Category="PostHog|Session Replay", meta = (ClampMin = 1, UIMin = 1, DisplayName = "Flush Event Count (Unavailable until SDKP-018)", ToolTip = "Unavailable until SDKP-018 implements session replay. This value remains serialized for future compatibility."))
	int32 FlushEventCount = 20;

	// Future interval for automatically flushing session replay events, in seconds.
	UPROPERTY(EditAnywhere, Category="PostHog|Session Replay", meta = (ClampMin = 1, UIMin = 1, DisplayName = "Flush Interval Seconds (Unavailable until SDKP-018)", ToolTip = "Unavailable until SDKP-018 implements session replay. This value remains serialized for future compatibility."))
	int32 FlushIntervalSeconds = 30;

	// Future maximum number of session replay events able to be stored in the queue.
	UPROPERTY(EditAnywhere, Category="PostHog|Session Replay", meta = (ClampMin = 1, UIMin = 1, DisplayName = "Max Queue Size (Unavailable until SDKP-018)", ToolTip = "Unavailable until SDKP-018 implements session replay. This value remains serialized for future compatibility."))
	int32 MaxQueueSize = 100;
};

/**
 * @class UPostHogDeveloperSettings
 * @brief Provides project-wide configuration settings for integrating PostHog analytics into Unreal Engine projects.
 *
 * This class allows developers to configure the implemented PostHog SDK behavior. Feature-flag and
 * session-replay settings are serialized for compatibility, but remain unavailable until SDKP-012 and
 * SDKP-018 implement their runtime subsystems.
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
	bool IsAnalyticsEnabled() const { return bAnalyticsEnabled; };
	
	UFUNCTION()
	const FString& GetApiKey() const { return ApiKey; }

	UFUNCTION()
	bool GetDefaultUserOptIn() const { return bDefaultUserOptIn; }

	UFUNCTION()
	int32 GetFlushEventCount() const { return FlushEventCount; }

	UFUNCTION()
	int32 GetFlushIntervalSeconds() const { return FlushIntervalSeconds; }

	UFUNCTION()
	int32 GetMaxQueueSize() const { return MaxQueueSize; }

	UFUNCTION()
	int32 GetMaxBatchSize() const { return MaxBatchSize; }

	UFUNCTION()
	bool ShouldCaptureApplicationLifecycleEvents() const { return bCaptureApplicationLifecycleEvents; }

	UFUNCTION()
	bool ShouldCaptureExceptions() const { return bCaptureExceptions; }

	UFUNCTION()
	bool ShouldCaptureExceptionsInEditor() const { return bCaptureExceptionsInEditor; }

	UFUNCTION()
	int32 GetExceptionDebounceIntervalMs() const { return ExceptionDebounceIntervalMs; }

	UFUNCTION()
	bool ShouldReuseAnonymousId() const { return bReuseAnonymousId; }

	UFUNCTION()
	EPostHogPersonProfiles GetPersonProfiles() const { return PersonProfiles; }

	UFUNCTION()
	EPostHogLogLevel GetLogLevel() const { return LogLevel; }

	UFUNCTION()
	bool ShouldPreloadFeatureFlags() const { return bPreloadFeatureFlags; }

	// Retries attempted after the initial feature-flag request, matching Unity's
	// FeatureFlagRequestMaxRetries. Zero means a single attempt.
	UFUNCTION()
	int32 GetFeatureFlagRequestMaxRetries() const { return FeatureFlagRequestMaxRetries; }

	UFUNCTION()
	bool IsSessionReplayEnabled() const { return bSessionReplay; }

	UFUNCTION()
	bool ShouldFlushOnQuit() const { return bFlushOnQuit; }

	UFUNCTION()
	float GetFlushOnQuitTimeoutSeconds() const { return FlushOnQuitTimeoutSeconds; }

#if WITH_EDITOR
	virtual bool CanEditChange(const FProperty* InProperty) const override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	
private:
	FString HostUS = "https://us.i.posthog.com";
	FString HostEU = "https://eu.i.posthog.com";
	
protected:
	// Developer kill switch for analytics collection, independent of the end user's opt-in consent.
	// When false, the SDK never collects regardless of user opt-in status.
	UPROPERTY(Config, EditAnywhere, Category="PostHog")
	bool bAnalyticsEnabled = true;

	// The public project API key from PostHog. Should start with "phc_". May be soft-validated in the future.
	UPROPERTY(Config, EditAnywhere, Category="PostHog", meta = (DisplayName = "Project Public API Key"))
	FString ApiKey = "";

	// The end user's default opt-in status for analytics collection before they have made an explicit choice.
	UPROPERTY(Config, EditAnywhere, Category="PostHog", meta = (DisplayName = "Default User Opt-In"))
	bool bDefaultUserOptIn = false;
	
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

	// Startup minimum severity for UnrealHog's own diagnostic logging. This value is applied to the
	// process-global `LogUnrealHog` category when the runtime subsystem initializes and acts as the
	// verbosity threshold: only messages at or above the selected severity are emitted.
	//
	// Level-to-verbosity mapping:
	//   Debug   -> VeryVerbose (all UnrealHog diagnostics, including high-volume per-record detail)
	//   Info    -> Log         (lifecycle/state transitions and above)
	//   Warning -> Warning     (actionable but nonfatal problems and errors) -- the default
	//   Error   -> Error       (only failures of accepted operations)
	//   None    -> NoLogging   (suppresses every UnrealHog diagnostic)
	//
	// This is the startup verbosity for the process. A standard Unreal console command such as
	// `Log LogUnrealHog VeryVerbose` may temporarily override the category after initialization for
	// live diagnostics without changing project configuration; a later subsystem initialization
	// reapplies this project setting deterministically.
	//
	// This setting controls the SDK's operational log output only. It is distinct from the
	// session-replay console-log capture threshold (`FPostHogSessionReplayConfig::MinLogLevel`,
	// owned by SDKP-016), which decides which game log lines are recorded into a replay.
	UPROPERTY(Config, EditAnywhere, Category="PostHog|Logging")
	EPostHogLogLevel LogLevel = EPostHogLogLevel::Warning;

	// Whether to reuse the anonymous ID across reset calls.
	UPROPERTY(Config, EditAnywhere, Category="PostHog|Identity")
	bool bReuseAnonymousId = false;

	// Serialized compatibility setting for future feature-flag preload support.
	UPROPERTY(Config, EditAnywhere, Category="PostHog|Feature Flags", meta = (DisplayName = "Preload Feature Flags (Unavailable until SDKP-012)", ToolTip = "Unavailable until SDKP-012 implements feature flags. This value remains serialized for future compatibility."))
	bool bPreloadFeatureFlags = true;

	// Serialized compatibility setting for future feature-flag request retries.
	UPROPERTY(Config, EditAnywhere, Category="PostHog|Feature Flags", meta = (ClampMin = 0, UIMin = 0, DisplayName = "Feature Flag Request Max Retries (Unavailable until SDKP-012)", ToolTip = "Unavailable until SDKP-012 implements feature flags. This value remains serialized for future compatibility."))
	int32 FeatureFlagRequestMaxRetries = 1;

	// Serialized compatibility setting for future $feature_flag_called events.
	UPROPERTY(Config, EditAnywhere, Category="PostHog|Feature Flags", meta = (DisplayName = "Send Feature Flag Event (Unavailable until SDKP-012)", ToolTip = "Unavailable until SDKP-012 implements feature flags. This value remains serialized for future compatibility."))
	bool bSendFeatureFlagEvent = true;

	// Serialized compatibility setting for future feature-flag request properties.
	UPROPERTY(Config, EditAnywhere, Category="PostHog|Feature Flags", meta = (DisplayName = "Send Default Person Properties For Flags (Unavailable until SDKP-012)", ToolTip = "Unavailable until SDKP-012 implements feature flags. This value remains serialized for future compatibility."))
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

	// Serialized compatibility setting for future session replay capture.
	UPROPERTY(Config, EditAnywhere, Category="PostHog|Session Replay", meta = (DisplayName = "Session Replay (Unavailable until SDKP-018)", ToolTip = "Unavailable until SDKP-018 implements session replay. This value remains serialized for future compatibility."))
	bool bSessionReplay = false;

	// Serialized compatibility settings used when future session replay is enabled.
	UPROPERTY(Config, EditAnywhere, Category="PostHog|Session Replay", meta = (EditCondition = "bSessionReplay", DisplayName = "Session Replay Config (Unavailable until SDKP-018)", ToolTip = "Unavailable until SDKP-018 implements session replay. This value remains serialized for future compatibility."))
	FPostHogSessionReplayConfig SessionReplayConfig;
};
