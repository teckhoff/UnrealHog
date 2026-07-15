// Trevor Eckhoff, 2026. All rights reserved.

#include "Subsystems/PostHogRuntimeSubsystem.h"

#include "Consent/PostHogConsentController.h"
#include "PostHogDeveloperSettings.h"
#include "Engine/World.h"
#include "Events/PostHogEvent.h"
#include "Http/PostHogHttpClient.h"
#include "Logging/PostHogLogger.h"
#include "Logging/StructuredLog.h"
#include "SDK/PostHogSdkInfo.h"
#include "Storage/PostHogStorageProvider.h"
#include "Events/PostHogEventProperties.h"
#include "TimerManager.h"
#include "Utilities/PostHogUuidV7.h"


bool UPostHogRuntimeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// The subsystem must always exist so consent can be granted at runtime even when analytics
	// is currently disabled or misconfigured; CaptureEvent/Flush remain safe no-ops until opt-in.
	return Super::ShouldCreateSubsystem(Outer);
}

void UPostHogRuntimeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	ConsentController = MakeUnique<FPostHogConsentController>(
		[]() { return IPostHogStorageProvider::CreateDefaultProvider(); },
		[](const FString& Host) -> TUniquePtr<IPostHogBatchTransport> { return MakeUnique<FPostHogHttpClient>(Host); },
		[]() { return PostHogUuidV7::New(); });

	ConsentController->Initialize(*GetDefault<UPostHogDeveloperSettings>());

	if (ConsentController->IsOptedIn())
	{
		const FString LibraryName = PostHogSdkInfo::GetLibraryName();
		const FString LibraryVersion = PostHogSdkInfo::GetPluginVersion();
		const FString UserAgent = PostHogSdkInfo::GetUserAgent();

		UE_LOGFMT(LogPostHog, Log, "PostHog Runtime Subsystem Initialized with active consent. Plugin {LibraryName} (ver. {Version}) ({Agent})", LibraryName, LibraryVersion, UserAgent);

		StartFlushTimer();
	}
	else
	{
		UE_LOGFMT(LogPostHog, Log, "PostHog Runtime Subsystem Initialized without consent; collection remains disabled until opt-in.");
	}
}

void UPostHogRuntimeSubsystem::Deinitialize()
{
	StopFlushTimer();

	if (ConsentController)
	{
		ConsentController->Shutdown();
	}

	Super::Deinitialize();
}

void UPostHogRuntimeSubsystem::CaptureEvent(const FString& EventName, UPostHogEventProperties* Properties)
{
	if (!ConsentController || !ConsentController->IsOptedIn())
	{
		UE_LOGFMT(LogPostHog, Warning, "PostHog Runtime Subsystem has no analytics consent; dropping event {EventName}.", EventName);
		return;
	}

	FPostHogEvent GeneratedEvent(EventName, ConsentController->GetSessionId());

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

	ConsentController->Capture(GeneratedEvent);
}

UPostHogEventProperties* UPostHogRuntimeSubsystem::CreateEventProperties()
{
	return NewObject<UPostHogEventProperties>(this);
}

void UPostHogRuntimeSubsystem::Flush()
{
	if (ConsentController)
	{
		ConsentController->Flush();
	}
}

void UPostHogRuntimeSubsystem::SetAnalyticsOptIn(bool bOptIn)
{
	if (!ConsentController)
	{
		return;
	}

	ConsentController->SetOptIn(bOptIn, *GetDefault<UPostHogDeveloperSettings>());

	if (ConsentController->IsOptedIn())
	{
		StartFlushTimer();
	}
	else
	{
		StopFlushTimer();
	}
}

bool UPostHogRuntimeSubsystem::IsAnalyticsOptedIn() const
{
	return ConsentController && ConsentController->IsOptedIn();
}

void UPostHogRuntimeSubsystem::FlushQueuedEvents()
{
	if (ConsentController)
	{
		UE_LOGFMT(LogPostHog, Log, "Timer Queue Flush!");
		ConsentController->Flush();
	}
}

void UPostHogRuntimeSubsystem::StartFlushTimer()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const UPostHogDeveloperSettings* Settings = GetDefault<UPostHogDeveloperSettings>();
	const float FlushIntervalSeconds = FMath::Max(static_cast<float>(Settings->GetFlushIntervalSeconds()), 1.0f);

	World->GetTimerManager().SetTimer(
		FlushTimerHandle,
		this,
		&UPostHogRuntimeSubsystem::FlushQueuedEvents,
		FlushIntervalSeconds,
		true,
		FlushIntervalSeconds);
}

void UPostHogRuntimeSubsystem::StopFlushTimer()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FlushTimerHandle);
	}
}
