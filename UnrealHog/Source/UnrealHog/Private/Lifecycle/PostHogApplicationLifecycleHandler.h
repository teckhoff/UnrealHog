#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

class IPostHogStorageProvider;
class UPostHogDeveloperSettings;
class UPostHogEventProperties;

struct FPostHogApplicationMetadata
{
	FString Version;
	FString Build;
};

/**
 * Private lifecycle producer. It normalizes overlapping Unreal application delegates into one
 * foreground/background state machine and emits Unity-parity lifecycle events only after consent.
 */
class FPostHogApplicationLifecycleHandler
{
public:
	using FMetadataProvider = TFunction<FPostHogApplicationMetadata()>;
	using FCaptureCallback = TFunction<void(const FString& EventName, UPostHogEventProperties* Properties)>;
	using FLifecycleCallback = TFunction<void()>;

	FPostHogApplicationLifecycleHandler(FCaptureCallback InCaptureCallback,
		FLifecycleCallback InForegroundCallback,
		FLifecycleCallback InBackgroundCallback,
		FMetadataProvider InMetadataProvider = nullptr);
	~FPostHogApplicationLifecycleHandler();

	void Start(const UPostHogDeveloperSettings& Settings, IPostHogStorageProvider& StorageProvider);
	void Stop();

	void NotifyApplicationForegrounded();
	void NotifyApplicationBackgrounded();
	void NotifyApplicationTerminating();

	static FPostHogApplicationMetadata CaptureDefaultMetadata();

private:
	void BindApplicationDelegates();
	void UnbindApplicationDelegates();

	void CheckVersionChanges();
	void LoadState(FString& OutLastSeenVersion, FString& OutLastSeenBuild) const;
	void SaveState(const FPostHogApplicationMetadata& Metadata) const;

	void CaptureApplicationInstalled(const FPostHogApplicationMetadata& Metadata) const;
	void CaptureApplicationUpdated(const FPostHogApplicationMetadata& Metadata,
		const FString& PreviousVersion,
		const FString& PreviousBuild) const;
	void CaptureApplicationOpened(const FPostHogApplicationMetadata& Metadata, bool bFromBackground) const;
	void CaptureApplicationBackgrounded(const FPostHogApplicationMetadata& Metadata) const;
	void CaptureEvent(const FString& EventName, UPostHogEventProperties* Properties) const;

	FCaptureCallback CaptureCallback;
	FLifecycleCallback ForegroundCallback;
	FLifecycleCallback BackgroundCallback;
	FMetadataProvider MetadataProvider;

	IPostHogStorageProvider* StorageProvider = nullptr;
	bool bStarted = false;
	bool bCaptureLifecycleEvents = false;
	bool bIsInForeground = true;

	FDelegateHandle ApplicationWillDeactivateHandle;
	FDelegateHandle ApplicationWillEnterBackgroundHandle;
	FDelegateHandle ApplicationHasReactivatedHandle;
	FDelegateHandle ApplicationHasEnteredForegroundHandle;
	FDelegateHandle ApplicationWillTerminateHandle;
};
