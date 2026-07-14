// Trevor Eckhoff, 2026. All rights reserved.

#include "Subsystems/PostHogRuntimeSubsystem.h"

#include "PostHogDeveloperSettings.h"
#include "PostHogSettingsValidation.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Events/PostHogEvent.h"
#include "Http/PostHogHttpClient.h"
#include "Logging/PostHogLogger.h"
#include "Logging/StructuredLog.h"
#include "SDK/PostHogSdkInfo.h"
#include "Storage/PostHogStorageProvider.h"
#include "Events/PostHogEventQueue.h"
#include "TimerManager.h"
#include "Events/PostHogEventProperties.h"
#include "Utilities/PostHogUuidV7.h"


bool UPostHogRuntimeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UPostHogDeveloperSettings* Settings = GetDefault<UPostHogDeveloperSettings>();

	return Settings->IsAnalyticsEnabled() && PostHogSettingsValidation::Validate(*Settings).bIsValid;
}

void UPostHogRuntimeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UPostHogDeveloperSettings* Settings = GetDefault<UPostHogDeveloperSettings>();

	const FPostHogSettingsValidationResult ValidationResult = PostHogSettingsValidation::Validate(*Settings);

	if (!ValidationResult.bIsValid)
	{
		UE_LOGFMT(LogPostHog, Warning, "PostHog Runtime Subsystem configuration is invalid ({Reason}); aborting initialization.", ValidationResult.FailureReason);
		return;
	}

	SessionId = PostHogUuidV7::New();

	if (SessionId.IsEmpty())
	{
		UE_LOGFMT(LogPostHog, Error, "PostHog Runtime Subsystem failed to generate session identifier; aborting initialization.");
		return;
	}

	const FString LibraryName = PostHogSdkInfo::GetLibraryName();
	const FString LibraryVersion = PostHogSdkInfo::GetPluginVersion();
	const FString UserAgent = PostHogSdkInfo::GetUserAgent();

	StorageProvider = IPostHogStorageProvider::CreateDefaultProvider();
	HttpClient = MakeUnique<FPostHogHttpClient>(ValidationResult.ResolvedHost);
	EventQueue = MakeUnique<FPostHogEventQueue>(*StorageProvider, *HttpClient, Settings->GetApiKey(), Settings->GetMaxQueueSize(), Settings->GetMaxBatchSize(), Settings->GetFlushEventCount());

	UE_LOGFMT(LogPostHog, Log, "PostHog Runtime Subsystem Initialized. Plugin {LibraryName} (ver. {Version}) ({Agent})", LibraryName, LibraryVersion, UserAgent);
	
	if (UWorld* World = GetWorld())
	{
		const float FlushIntervalSeconds = FMath::Max(static_cast<float>(Settings->GetFlushIntervalSeconds()), 1.0f);
		World->GetTimerManager().SetTimer(
			FlushTimerHandle,
			this,
			&UPostHogRuntimeSubsystem::FlushQueuedEvents,
			FlushIntervalSeconds,
			true,
			FlushIntervalSeconds);
	}
}

void UPostHogRuntimeSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FlushTimerHandle);
	}
	
	if (EventQueue)
	{
		EventQueue->CancelInFlightRequest();
	}
	
	Super::Deinitialize();
}

void UPostHogRuntimeSubsystem::CaptureEvent(const FString& EventName, UPostHogEventProperties* Properties)
{
	if (!EventQueue)
	{
		UE_LOGFMT(LogPostHog, Warning, "PostHog Runtime Subsystem not initialized; dropping event {EventName}.", EventName);
		return;
	}

	FPostHogEvent GeneratedEvent(EventName, SessionId);

	if (GeneratedEvent.GetEventId().IsEmpty())
	{
		UE_LOGFMT(LogPostHog, Error, "PostHog Runtime Subsystem failed to generate an event identifier; dropping event {EventName}.", EventName);
		return;
	}

	// TODO: Check opt-in/opt-out status to determine whether to ProcessProfile.
	// Currently, default to anonymous events.
	GeneratedEvent.SetProcessPersonProfile(false);
	
	if (Properties)
	{
		Properties->ApplyToEvent(GeneratedEvent);
	}
	
	EventQueue->Enqueue(GeneratedEvent);
}

UPostHogEventProperties* UPostHogRuntimeSubsystem::CreateEventProperties()
{
	return NewObject<UPostHogEventProperties>(this);
}

void UPostHogRuntimeSubsystem::FlushQueuedEvents()
{
	if (EventQueue)
	{
		UE_LOGFMT(LogPostHog, Log, "Timer Queue Flush!");
		EventQueue->Flush();
	}
}
