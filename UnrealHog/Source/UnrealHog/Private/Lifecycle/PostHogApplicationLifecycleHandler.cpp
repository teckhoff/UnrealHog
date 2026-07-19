#include "Lifecycle/PostHogApplicationLifecycleHandler.h"

#include "Dom/JsonObject.h"
#include "Events/PostHogEventContext.h"
#include "Events/PostHogEventProperties.h"
#include "Logging/PostHogLogger.h"
#include "Misc/CoreDelegates.h"
#include "PostHogDeveloperSettings.h"
#include "Serialization/JsonSerializer.h"
#include "Storage/PostHogStorageProvider.h"
#include "UObject/Package.h"

namespace
{
	const FString LifecycleStateKey = TEXT("lifecycle");
	const FString LastSeenVersionFieldName = TEXT("lastSeenVersion");
	const FString LastSeenBuildFieldName = TEXT("lastSeenBuild");

	UPostHogEventProperties* MakeVersionBuildProperties(const FPostHogApplicationMetadata& Metadata)
	{
		UPostHogEventProperties* Properties = NewObject<UPostHogEventProperties>(GetTransientPackage());
		Properties->AddString(TEXT("version"), Metadata.Version);
		Properties->AddString(TEXT("build"), Metadata.Build);
		return Properties;
	}
}

FPostHogApplicationLifecycleHandler::FPostHogApplicationLifecycleHandler(FCaptureCallback InCaptureCallback,
	FLifecycleCallback InForegroundCallback,
	FLifecycleCallback InBackgroundCallback,
	FMetadataProvider InMetadataProvider) :
	CaptureCallback(MoveTemp(InCaptureCallback)),
	ForegroundCallback(MoveTemp(InForegroundCallback)),
	BackgroundCallback(MoveTemp(InBackgroundCallback)),
	MetadataProvider(InMetadataProvider ? MoveTemp(InMetadataProvider) : FMetadataProvider(&FPostHogApplicationLifecycleHandler::CaptureDefaultMetadata))
{
}

FPostHogApplicationLifecycleHandler::~FPostHogApplicationLifecycleHandler()
{
	Stop();
}

void FPostHogApplicationLifecycleHandler::Start(const UPostHogDeveloperSettings& Settings, IPostHogStorageProvider& InStorageProvider)
{
	if (bStarted)
	{
		Stop();
	}

	StorageProvider = &InStorageProvider;
	bStarted = true;
	bCaptureLifecycleEvents = Settings.ShouldCaptureApplicationLifecycleEvents();
	bIsInForeground = true;

	BindApplicationDelegates();

	if (bCaptureLifecycleEvents)
	{
		CheckVersionChanges();
	}
}

void FPostHogApplicationLifecycleHandler::Stop()
{
	if (!bStarted)
	{
		return;
	}

	UnbindApplicationDelegates();
	StorageProvider = nullptr;
	bStarted = false;
	bCaptureLifecycleEvents = false;
	bIsInForeground = true;
}

void FPostHogApplicationLifecycleHandler::NotifyApplicationForegrounded()
{
	if (!bStarted || bIsInForeground)
	{
		return;
	}

	bIsInForeground = true;

	if (ForegroundCallback)
	{
		ForegroundCallback();
	}

	if (bCaptureLifecycleEvents)
	{
		CaptureApplicationOpened(MetadataProvider(), /*bFromBackground=*/true);
	}
}

void FPostHogApplicationLifecycleHandler::NotifyApplicationBackgrounded()
{
	if (!bStarted || !bIsInForeground)
	{
		return;
	}

	bIsInForeground = false;

	if (BackgroundCallback)
	{
		BackgroundCallback();
	}

	if (bCaptureLifecycleEvents)
	{
		CaptureApplicationBackgrounded(MetadataProvider());
	}
}

void FPostHogApplicationLifecycleHandler::NotifyApplicationTerminating()
{
	NotifyApplicationBackgrounded();
}

FPostHogApplicationMetadata FPostHogApplicationLifecycleHandler::CaptureDefaultMetadata()
{
	const FPostHogEventContext EventContext = FPostHogEventContextProvider::Capture();

	FPostHogApplicationMetadata Metadata;
	Metadata.Version = EventContext.AppVersion;
	Metadata.Build = EventContext.AppBuild;

	return Metadata;
}

void FPostHogApplicationLifecycleHandler::BindApplicationDelegates()
{
	ApplicationWillDeactivateHandle = FCoreDelegates::ApplicationWillDeactivateDelegate.AddRaw(this, &FPostHogApplicationLifecycleHandler::NotifyApplicationBackgrounded);
	ApplicationWillEnterBackgroundHandle = FCoreDelegates::ApplicationWillEnterBackgroundDelegate.AddRaw(this, &FPostHogApplicationLifecycleHandler::NotifyApplicationBackgrounded);
	ApplicationHasReactivatedHandle = FCoreDelegates::ApplicationHasReactivatedDelegate.AddRaw(this, &FPostHogApplicationLifecycleHandler::NotifyApplicationForegrounded);
	ApplicationHasEnteredForegroundHandle = FCoreDelegates::ApplicationHasEnteredForegroundDelegate.AddRaw(this, &FPostHogApplicationLifecycleHandler::NotifyApplicationForegrounded);
	ApplicationWillTerminateHandle = FCoreDelegates::GetApplicationWillTerminateDelegate().AddRaw(this, &FPostHogApplicationLifecycleHandler::NotifyApplicationTerminating);
}

void FPostHogApplicationLifecycleHandler::UnbindApplicationDelegates()
{
	if (ApplicationWillDeactivateHandle.IsValid())
	{
		FCoreDelegates::ApplicationWillDeactivateDelegate.Remove(ApplicationWillDeactivateHandle);
		ApplicationWillDeactivateHandle.Reset();
	}

	if (ApplicationWillEnterBackgroundHandle.IsValid())
	{
		FCoreDelegates::ApplicationWillEnterBackgroundDelegate.Remove(ApplicationWillEnterBackgroundHandle);
		ApplicationWillEnterBackgroundHandle.Reset();
	}

	if (ApplicationHasReactivatedHandle.IsValid())
	{
		FCoreDelegates::ApplicationHasReactivatedDelegate.Remove(ApplicationHasReactivatedHandle);
		ApplicationHasReactivatedHandle.Reset();
	}

	if (ApplicationHasEnteredForegroundHandle.IsValid())
	{
		FCoreDelegates::ApplicationHasEnteredForegroundDelegate.Remove(ApplicationHasEnteredForegroundHandle);
		ApplicationHasEnteredForegroundHandle.Reset();
	}

	if (ApplicationWillTerminateHandle.IsValid())
	{
		FCoreDelegates::GetApplicationWillTerminateDelegate().Remove(ApplicationWillTerminateHandle);
		ApplicationWillTerminateHandle.Reset();
	}
}

void FPostHogApplicationLifecycleHandler::CheckVersionChanges()
{
	if (!bStarted || !bCaptureLifecycleEvents || !StorageProvider)
	{
		return;
	}

	FString LastSeenVersion;
	FString LastSeenBuild;
	LoadState(LastSeenVersion, LastSeenBuild);

	const FPostHogApplicationMetadata CurrentMetadata = MetadataProvider();

	if (LastSeenVersion.IsEmpty())
	{
		CaptureApplicationInstalled(CurrentMetadata);
	}
	else if (LastSeenVersion != CurrentMetadata.Version || LastSeenBuild != CurrentMetadata.Build)
	{
		CaptureApplicationUpdated(CurrentMetadata, LastSeenVersion, LastSeenBuild);
	}

	SaveState(CurrentMetadata);
	CaptureApplicationOpened(CurrentMetadata, /*bFromBackground=*/false);
}

void FPostHogApplicationLifecycleHandler::LoadState(FString& OutLastSeenVersion, FString& OutLastSeenBuild) const
{
	OutLastSeenVersion.Empty();
	OutLastSeenBuild.Empty();

	if (!bStarted || !bCaptureLifecycleEvents || !StorageProvider)
	{
		return;
	}

	FString StateJson;
	if (!StorageProvider->LoadState(LifecycleStateKey, StateJson))
	{
		return;
	}

	TSharedPtr<FJsonObject> StateObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(StateJson);
	if (!FJsonSerializer::Deserialize(Reader, StateObject) || !StateObject.IsValid())
	{
		UE_LOG(LogUnrealHog, Warning, TEXT("Failed to parse PostHog lifecycle state."));
		return;
	}

	StateObject->TryGetStringField(LastSeenVersionFieldName, OutLastSeenVersion);
	StateObject->TryGetStringField(LastSeenBuildFieldName, OutLastSeenBuild);
}

void FPostHogApplicationLifecycleHandler::SaveState(const FPostHogApplicationMetadata& Metadata) const
{
	if (!bStarted || !bCaptureLifecycleEvents || !StorageProvider)
	{
		return;
	}

	const TSharedRef<FJsonObject> StateObject = MakeShared<FJsonObject>();
	StateObject->SetStringField(LastSeenVersionFieldName, Metadata.Version);
	StateObject->SetStringField(LastSeenBuildFieldName, Metadata.Build);

	if (!StorageProvider->SaveState(LifecycleStateKey, StateObject))
	{
		UE_LOG(LogUnrealHog, Warning, TEXT("Failed to save PostHog lifecycle state."));
	}
}

void FPostHogApplicationLifecycleHandler::CaptureApplicationInstalled(const FPostHogApplicationMetadata& Metadata) const
{
	UPostHogEventProperties* Properties = MakeVersionBuildProperties(Metadata);
	CaptureEvent(TEXT("Application Installed"), Properties);
}

void FPostHogApplicationLifecycleHandler::CaptureApplicationUpdated(const FPostHogApplicationMetadata& Metadata,
	const FString& PreviousVersion,
	const FString& PreviousBuild) const
{
	UPostHogEventProperties* Properties = MakeVersionBuildProperties(Metadata);
	Properties->AddString(TEXT("previous_version"), PreviousVersion);
	Properties->AddString(TEXT("previous_build"), PreviousBuild);
	CaptureEvent(TEXT("Application Updated"), Properties);
}

void FPostHogApplicationLifecycleHandler::CaptureApplicationOpened(const FPostHogApplicationMetadata& Metadata, bool bFromBackground) const
{
	UPostHogEventProperties* Properties = MakeVersionBuildProperties(Metadata);
	Properties->AddBoolean(TEXT("from_background"), bFromBackground);
	CaptureEvent(TEXT("Application Opened"), Properties);
}

void FPostHogApplicationLifecycleHandler::CaptureApplicationBackgrounded(const FPostHogApplicationMetadata& Metadata) const
{
	UPostHogEventProperties* Properties = MakeVersionBuildProperties(Metadata);
	CaptureEvent(TEXT("Application Backgrounded"), Properties);
}

void FPostHogApplicationLifecycleHandler::CaptureEvent(const FString& EventName, UPostHogEventProperties* Properties) const
{
	if (CaptureCallback)
	{
		CaptureCallback(EventName, Properties);
	}
}
