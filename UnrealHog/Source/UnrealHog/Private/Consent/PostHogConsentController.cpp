#include "Consent/PostHogConsentController.h"

#include "Dom/JsonObject.h"
#include "Events/PostHogCapturePolicy.h"
#include "Events/PostHogEvent.h"
#include "Events/PostHogEventProperties.h"
#include "Events/PostHogEventQueue.h"
#include "Events/PostHogExceptionPropertiesBuilder.h"
#include "Http/PostHogBatchTransport.h"
#include "Lifecycle/PostHogApplicationLifecycleHandler.h"
#include "Identity/PostHogIdentityManager.h"
#include "Logging/PostHogLogger.h"
#include "Logging/StructuredLog.h"
#include "Misc/CoreDelegates.h"
#include "PostHogDeveloperSettings.h"
#include "PostHogSettingsValidation.h"
#include "Serialization/JsonSerializer.h"
#include "Session/PostHogSessionManager.h"
#include "Storage/PostHogStorageProvider.h"
#include "SuperProperties/PostHogSuperPropertiesManager.h"

namespace
{
	const FString OptInStateKey = TEXT("opt_in_status");
	const FString OptedInFieldName = TEXT("opted_in");
}

FPostHogConsentController::FPostHogConsentController(FStorageProviderFactory InStorageProviderFactory,
	FTransportFactory InTransportFactory,
	FUuidGenerator InUuidGenerator,
	FLifecycleMetadataProvider InLifecycleMetadataProvider) :
	StorageProviderFactory(MoveTemp(InStorageProviderFactory)),
	TransportFactory(MoveTemp(InTransportFactory)),
	UuidGenerator(MoveTemp(InUuidGenerator)),
	LifecycleMetadataProvider(MoveTemp(InLifecycleMetadataProvider))
{
	SessionManager = MakeUnique<FPostHogSessionManager>();
}

FPostHogConsentController::~FPostHogConsentController()
{
	Shutdown();
}

void FPostHogConsentController::Initialize(const UPostHogDeveloperSettings& Settings)
{
	TUniquePtr<IPostHogStorageProvider> ProbeStorage = StorageProviderFactory();
	++StorageProviderCreationCount;

	bool bShouldOptIn = false;
	if (!TryLoadPersistedOptIn(*ProbeStorage, bShouldOptIn))
	{
		bShouldOptIn = Settings.GetDefaultUserOptIn();
	}

	if (!bShouldOptIn)
	{
		return;
	}

	StorageProvider = MoveTemp(ProbeStorage);

	FString FailureReason;
	if (!EnableCollection(Settings, FailureReason))
	{
		UE_LOGFMT(LogUnrealHog, Warning, "PostHog Consent Controller could not restore opt-in state ({Reason}); remaining opted out.", FailureReason);
		StorageProvider.Reset();
	}
}

void FPostHogConsentController::Shutdown()
{
	if (BackgroundFlushHandle.IsValid())
	{
		FCoreDelegates::ApplicationWillEnterBackgroundDelegate.Remove(BackgroundFlushHandle);
		BackgroundFlushHandle.Reset();
	}

	if (TerminateShutdownHandle.IsValid())
	{
		FCoreDelegates::GetApplicationWillTerminateDelegate().Remove(TerminateShutdownHandle);
		TerminateShutdownHandle.Reset();
	}

	bIsShuttingDown = true;

	if (LifecycleHandler)
	{
		LifecycleHandler->Stop();
	}

	if (EventQueue)
	{
		EventQueue->CancelInFlightRequest();
	}

	// Storage-only finalize: this path (also reached via Deinitialize, OnEnginePreExit, and the
	// terminate delegate binding above) must never initiate network I/O once engine teardown may
	// be underway.
	DrainPendingStorageWrites();
}

bool FPostHogConsentController::SetOptIn(bool bOptIn, const UPostHogDeveloperSettings& Settings)
{
	if (!bOptIn)
	{
		if (bIsOptedIn)
		{
			DisableCollection();
		}
		return true;
	}

	if (bIsOptedIn)
	{
		return true;
	}

	FString FailureReason;
	const bool bOk = EnableCollection(Settings, FailureReason);
	if (!bOk)
	{
		UE_LOGFMT(LogUnrealHog, Warning, "PostHog Consent Controller rejected opt-in ({Reason}).", FailureReason);
	}
	return bOk;
}

bool FPostHogConsentController::Capture(const FPostHogEvent& Event)
{
	if (!bIsOptedIn || !EventQueue.IsValid())
	{
		return false;
	}

	return EventQueue->Enqueue(Event) == EPostHogEventQueueEnqueueResult::Enqueued;
}

void FPostHogConsentController::SetBeforeSend(FPostHogBeforeSendDelegate InBeforeSend)
{
	BeforeSend = MoveTemp(InBeforeSend);
}

void FPostHogConsentController::ClearBeforeSend()
{
	BeforeSend.Unbind();
}

EPostHogCaptureResult FPostHogConsentController::CaptureEvent(const FString& EventName, UPostHogEventProperties* CallProperties)
{
	return CaptureEventWithProducerProperties(EventName, CallProperties, [](FPostHogEvent&) {});
}

EPostHogCaptureResult FPostHogConsentController::CaptureScreen(const FString& ScreenName, UPostHogEventProperties* CallProperties)
{
	if (!PostHogCapturePolicy::IsValidEventName(ScreenName))
	{
		UE_LOGFMT(LogUnrealHog, Warning, "PostHog Consent Controller rejected screen capture with an empty or whitespace-only screen name.");
		return EPostHogCaptureResult::InvalidEventName;
	}

	return CaptureEventWithProducerProperties(TEXT("$screen"), CallProperties, [&ScreenName](FPostHogEvent& Event)
	{
		Event.SetStringProperty(TEXT("$screen_name"), ScreenName);
	});
}

EPostHogCaptureResult FPostHogConsentController::CaptureEventWithProducerProperties(const FString& EventName,
	UPostHogEventProperties* CallProperties,
	TFunctionRef<void(FPostHogEvent&)> ApplyProducerProperties)
{
	if (!PostHogCapturePolicy::IsValidEventName(EventName))
	{
		UE_LOGFMT(LogUnrealHog, Warning, "PostHog Consent Controller rejected capture with an empty or whitespace-only event name.");
		return EPostHogCaptureResult::InvalidEventName;
	}

	if (!bIsOptedIn || !EventQueue.IsValid())
	{
		UE_LOGFMT(LogUnrealHog, Warning, "PostHog Consent Controller has no analytics consent; dropping event {EventName}.", EventName);
		return EPostHogCaptureResult::NotOptedIn;
	}

	FPostHogEvent GeneratedEvent(EventName, IdentityManager->GetEffectiveDistinctId());

	ApplySuperProperties(GeneratedEvent);

	if (CallProperties)
	{
		CallProperties->ApplyToEvent(GeneratedEvent);
	}

	ApplyProducerProperties(GeneratedEvent);

	const bool bProcessPersonProfile = PostHogCapturePolicy::ShouldProcessPersonProfile(PersonProfilesPolicy, IdentityManager->IsIdentified());
	GeneratedEvent.ApplySdkProperties(bProcessPersonProfile);

	const FString CurrentSessionId = SessionManager->GetSessionId();
	if (!CurrentSessionId.IsEmpty())
	{
		GeneratedEvent.SetStringProperty(TEXT("$session_id"), CurrentSessionId);
	}

	ApplyGroups(GeneratedEvent);

	const EPostHogBeforeSendResult BeforeSendResult = GeneratedEvent.RunBeforeSend(BeforeSend);
	if (BeforeSendResult == EPostHogBeforeSendResult::Drop)
	{
		return EPostHogCaptureResult::DroppedByBeforeSend;
	}

	if (BeforeSendResult == EPostHogBeforeSendResult::Failure)
	{
		UE_LOGFMT(LogUnrealHog, Error, "PostHog before-send callback reported failure for event {EventName}; dropping event before persistence.", EventName);
		return EPostHogCaptureResult::BeforeSendFailed;
	}

	if (!Capture(GeneratedEvent))
	{
		UE_LOGFMT(LogUnrealHog, Error, "PostHog Consent Controller failed to enqueue event {EventName}.", EventName);
		return EPostHogCaptureResult::EnqueueFailed;
	}

	SessionManager->Touch();

	return EPostHogCaptureResult::Success;
}

EPostHogCaptureResult FPostHogConsentController::Identify(const FString& DistinctId, UPostHogEventProperties* UserProperties, UPostHogEventProperties* UserPropertiesSetOnce)
{
	if (DistinctId.TrimStartAndEnd().IsEmpty())
	{
		UE_LOGFMT(LogUnrealHog, Warning, "PostHog Consent Controller rejected Identify with an empty or whitespace-only distinct id.");
		return EPostHogCaptureResult::InvalidEventName;
	}

	if (!bIsOptedIn || !IdentityManager.IsValid() || !StorageProvider.IsValid())
	{
		UE_LOGFMT(LogUnrealHog, Warning, "PostHog Consent Controller has no analytics consent; dropping Identify for {DistinctId}.", DistinctId);
		return EPostHogCaptureResult::NotOptedIn;
	}

	const FString PreviousAnonymousId = IdentityManager->Identify(DistinctId, *StorageProvider);

	UPostHogEventProperties* Props = NewObject<UPostHogEventProperties>();
	if (!PreviousAnonymousId.IsEmpty())
	{
		Props->AddString(TEXT("$anon_distinct_id"), PreviousAnonymousId);
	}
	if (UserProperties && UserProperties->GetProperties().Num() > 0)
	{
		Props->AddObject(TEXT("$set"), UserProperties);
	}
	if (UserPropertiesSetOnce && UserPropertiesSetOnce->GetProperties().Num() > 0)
	{
		Props->AddObject(TEXT("$set_once"), UserPropertiesSetOnce);
	}

	return CaptureEvent(TEXT("$identify"), Props);
}

void FPostHogConsentController::Reset(const UPostHogDeveloperSettings& Settings)
{
	if (!bIsOptedIn || !IdentityManager.IsValid() || !StorageProvider.IsValid())
	{
		return;
	}

	IdentityManager->Reset(*StorageProvider, Settings.ShouldReuseAnonymousId());

	if (SessionManager.IsValid())
	{
		SessionManager->StartNewSession();
	}
}

EPostHogCaptureResult FPostHogConsentController::Alias(const FString& Alias)
{
	if (Alias.TrimStartAndEnd().IsEmpty())
	{
		UE_LOGFMT(LogUnrealHog, Warning, "PostHog Consent Controller rejected Alias with an empty or whitespace-only alias.");
		return EPostHogCaptureResult::InvalidEventName;
	}

	UPostHogEventProperties* Props = NewObject<UPostHogEventProperties>();
	Props->AddString(TEXT("alias"), Alias);

	return CaptureEvent(TEXT("$create_alias"), Props);
}

EPostHogCaptureResult FPostHogConsentController::Group(const FString& GroupType, const FString& GroupKey, UPostHogEventProperties* GroupProperties)
{
	const FString TrimmedType = GroupType.TrimStartAndEnd();
	const FString TrimmedKey = GroupKey.TrimStartAndEnd();

	if (TrimmedType.IsEmpty() || TrimmedKey.IsEmpty())
	{
		UE_LOGFMT(LogUnrealHog, Warning, "PostHog Consent Controller rejected Group with an empty or whitespace-only group type or key.");
		return EPostHogCaptureResult::InvalidEventName;
	}

	if (!bIsOptedIn || !IdentityManager.IsValid() || !StorageProvider.IsValid())
	{
		UE_LOGFMT(LogUnrealHog, Warning, "PostHog Consent Controller has no analytics consent; dropping Group for {GroupType}.", TrimmedType);
		return EPostHogCaptureResult::NotOptedIn;
	}

	IdentityManager->SetGroup(TrimmedType, TrimmedKey, *StorageProvider);

	UPostHogEventProperties* Props = NewObject<UPostHogEventProperties>();
	Props->AddString(TEXT("$group_type"), TrimmedType);
	Props->AddString(TEXT("$group_key"), TrimmedKey);
	if (GroupProperties && GroupProperties->GetProperties().Num() > 0)
	{
		Props->AddObject(TEXT("$group_set"), GroupProperties);
	}

	return CaptureEvent(TEXT("$groupidentify"), Props);
}

EPostHogCaptureResult FPostHogConsentController::CaptureException(const FPostHogExceptionInput& Exception, UPostHogEventProperties* Properties)
{
	if (Exception.Message.TrimStartAndEnd().IsEmpty() || Exception.Type.TrimStartAndEnd().IsEmpty())
	{
		UE_LOGFMT(LogUnrealHog, Warning, "PostHog Consent Controller rejected CaptureException with an empty or whitespace-only message or type.");
		return EPostHogCaptureResult::InvalidEventName;
	}

	UPostHogEventProperties* Props = NewObject<UPostHogEventProperties>();
	Props->AppendFrom(Properties);
	PostHogExceptionPropertiesBuilder::Build(*Props, Exception);

	return CaptureEvent(TEXT("$exception"), Props);
}

void FPostHogConsentController::ResetGroups()
{
	if (!bIsOptedIn || !IdentityManager.IsValid() || !StorageProvider.IsValid())
	{
		return;
	}

	IdentityManager->ClearGroups(*StorageProvider);
}

bool FPostHogConsentController::RegisterSuperProperty(const FString& Key, const FPostHogEventProperty& Value)
{
	if (!bIsOptedIn || !SuperPropertiesManager.IsValid() || !StorageProvider.IsValid())
	{
		UE_LOGFMT(LogUnrealHog, Warning, "PostHog Consent Controller has no analytics consent; dropping RegisterSuperProperty for {Key}.", Key);
		return false;
	}

	SuperPropertiesManager->Register(Key, Value, *StorageProvider);
	return true;
}

void FPostHogConsentController::UnregisterSuperProperty(const FString& Key)
{
	if (!bIsOptedIn || !SuperPropertiesManager.IsValid() || !StorageProvider.IsValid())
	{
		UE_LOGFMT(LogUnrealHog, Warning, "PostHog Consent Controller has no analytics consent; dropping UnregisterSuperProperty for {Key}.", Key);
		return;
	}

	SuperPropertiesManager->Unregister(Key, *StorageProvider);
}

void FPostHogConsentController::ClearSuperProperties()
{
	if (!bIsOptedIn || !SuperPropertiesManager.IsValid() || !StorageProvider.IsValid())
	{
		UE_LOGFMT(LogUnrealHog, Warning, "PostHog Consent Controller has no analytics consent; dropping ClearSuperProperties.");
		return;
	}

	SuperPropertiesManager->Clear(*StorageProvider);
}

void FPostHogConsentController::ApplySuperProperties(FPostHogEvent& Event) const
{
	if (SuperPropertiesManager)
	{
		SuperPropertiesManager->ApplyTo(Event);
	}
}

void FPostHogConsentController::ApplyGroups(FPostHogEvent& Event) const
{
	if (!IdentityManager.IsValid())
	{
		return;
	}

	const TMap<FString, FString> CurrentGroups = IdentityManager->GetGroups();
	if (CurrentGroups.Num() == 0)
	{
		return;
	}

	FJsonObject GroupsObject;
	for (const auto& GroupPair : CurrentGroups)
	{
		GroupsObject.SetStringField(GroupPair.Key, GroupPair.Value);
	}
	Event.SetObjectProperty(TEXT("$groups"), GroupsObject);
}

void FPostHogConsentController::Flush(FPostHogEventQueueFlushComplete OnComplete)
{
	if (EventQueue.IsValid())
	{
		EventQueue->Flush(MoveTemp(OnComplete));
		return;
	}

	if (OnComplete)
	{
		OnComplete(EPostHogEventQueueFlushResult::Empty);
	}
}

EPostHogConsentFlushRequestResult FPostHogConsentController::RequestFlush(FPostHogEventQueueFlushComplete OnComplete)
{
	if (bIsShuttingDown || !bIsOptedIn || !EventQueue.IsValid())
	{
		if (OnComplete)
		{
			OnComplete(EPostHogEventQueueFlushResult::Empty);
		}
		return EPostHogConsentFlushRequestResult::Skipped;
	}

	const bool bAlreadyFlushing = EventQueue->IsFlushing();
	if (!bAlreadyFlushing && !EventQueue->HasPendingEvents())
	{
		if (OnComplete)
		{
			OnComplete(EPostHogEventQueueFlushResult::Empty);
		}
		return EPostHogConsentFlushRequestResult::Skipped;
	}

	EventQueue->Flush(MoveTemp(OnComplete));
	return bAlreadyFlushing ? EPostHogConsentFlushRequestResult::AlreadyInProgress : EPostHogConsentFlushRequestResult::Started;
}

int32 FPostHogConsentController::GetQueuedEventCount() const
{
	return EventQueue.IsValid() ? EventQueue->Num() : 0;
}

FString FPostHogConsentController::GetSessionId()
{
	return SessionManager->GetSessionId();
}

const FString& FPostHogConsentController::GetDistinctId() const
{
	static const FString EmptyDistinctId;
	return IdentityManager.IsValid() ? IdentityManager->GetEffectiveDistinctId() : EmptyDistinctId;
}

void FPostHogConsentController::NotifyApplicationForegrounded()
{
	SessionManager->OnForeground();
}

void FPostHogConsentController::NotifyApplicationBackgrounded()
{
	SessionManager->OnBackground();
}

void FPostHogConsentController::DrainPendingStorageWrites()
{
	if (StorageProvider.IsValid())
	{
		StorageProvider->FlushPendingWrites();
	}
}

void FPostHogConsentController::HandleApplicationEnteringBackground()
{
	// Session activity is already stopped by LifecycleHandler's own binding to this same
	// delegate (NotifyApplicationBackgrounded -> SessionManager->OnBackground()); this handler
	// only adds the flush/drain behavior required by EP-027.

	// Best-effort: the engine is still alive here so starting a request is safe, but on iOS the
	// process may be suspended moments later, so this is a latency optimization only.
	RequestFlush({});

	// The durability guarantee: synchronously ensure queued events are durably persisted before
	// the process may be suspended.
	DrainPendingStorageWrites();
}

bool FPostHogConsentController::EnableCollection(const UPostHogDeveloperSettings& Settings, FString& OutFailureReason)
{
	if (!Settings.IsAnalyticsEnabled())
	{
		OutFailureReason = TEXT("analytics disabled by developer setting");
		return false;
	}

	const FPostHogSettingsValidationResult ValidationResult = PostHogSettingsValidation::Validate(Settings);
	if (!ValidationResult.bIsValid)
	{
		OutFailureReason = ValidationResult.FailureReason;
		return false;
	}

	PersonProfilesPolicy = ValidationResult.PersonProfiles;

	if (!StorageProvider.IsValid())
	{
		StorageProvider = StorageProviderFactory();
		++StorageProviderCreationCount;
	}

	IdentityManager = MakeUnique<FPostHogIdentityManager>(UuidGenerator);
	IdentityManager->LoadOrCreate(*StorageProvider);
	++IdentityManagerLoadCount;

	SuperPropertiesManager = MakeUnique<FPostHogSuperPropertiesManager>();
	SuperPropertiesManager->LoadOrCreate(*StorageProvider);

	if (IdentityManager->GetAnonymousId().IsEmpty())
	{
		OutFailureReason = TEXT("failed to generate distinct identifier");
		IdentityManager.Reset();
		SuperPropertiesManager.Reset();
		StorageProvider.Reset();
		return false;
	}

	Transport = TransportFactory(ValidationResult.ResolvedHost);
	++TransportCreationCount;

	EventQueue = MakeUnique<FPostHogEventQueue>(*StorageProvider, *Transport, Settings.GetApiKey(),
		Settings.GetMaxQueueSize(), Settings.GetMaxBatchSize(), Settings.GetFlushEventCount());

	bIsOptedIn = true;
	PersistOptIn(true);
	SessionManager->SetCollectionPermitted(true);

	LifecycleHandler = MakeUnique<FPostHogApplicationLifecycleHandler>(
		[this](const FString& EventName, UPostHogEventProperties* Properties)
		{
			CaptureEvent(EventName, Properties);
		},
		[this]()
		{
			SessionManager->OnForeground();
		},
		[this]()
		{
			SessionManager->OnBackground();
		},
		LifecycleMetadataProvider);
	LifecycleHandler->Start(Settings, *StorageProvider);

	BackgroundFlushHandle = FCoreDelegates::ApplicationWillEnterBackgroundDelegate.AddRaw(this, &FPostHogConsentController::HandleApplicationEnteringBackground);
	TerminateShutdownHandle = FCoreDelegates::GetApplicationWillTerminateDelegate().AddRaw(this, &FPostHogConsentController::Shutdown);

	return true;
}

void FPostHogConsentController::DisableCollection()
{
	if (BackgroundFlushHandle.IsValid())
	{
		FCoreDelegates::ApplicationWillEnterBackgroundDelegate.Remove(BackgroundFlushHandle);
		BackgroundFlushHandle.Reset();
	}

	if (TerminateShutdownHandle.IsValid())
	{
		FCoreDelegates::GetApplicationWillTerminateDelegate().Remove(TerminateShutdownHandle);
		TerminateShutdownHandle.Reset();
	}

	if (LifecycleHandler)
	{
		LifecycleHandler->Stop();
		LifecycleHandler.Reset();
	}

	bIsOptedIn = false;

	if (EventQueue)
	{
		EventQueue->Clear();
	}

	PersistOptIn(false);

	EventQueue.Reset();
	Transport.Reset();
	StorageProvider.Reset();
	IdentityManager.Reset();
	SuperPropertiesManager.Reset();
	SessionManager->SetCollectionPermitted(false);
	SessionManager->EndSession();
}

void FPostHogConsentController::PersistOptIn(bool bOptIn) const
{
	if (!StorageProvider.IsValid())
	{
		return;
	}

	const TSharedRef<FJsonObject> StateObject = MakeShared<FJsonObject>();
	StateObject->SetBoolField(OptedInFieldName, bOptIn);
	StorageProvider->SaveState(OptInStateKey, StateObject);
}

bool FPostHogConsentController::TryLoadPersistedOptIn(IPostHogStorageProvider& StorageProvider, bool& OutOptedIn)
{
	FString StateJson;
	if (!StorageProvider.LoadState(OptInStateKey, StateJson))
	{
		return false;
	}

	TSharedPtr<FJsonObject> StateObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(StateJson);
	if (!FJsonSerializer::Deserialize(Reader, StateObject) || !StateObject.IsValid())
	{
		return false;
	}

	return StateObject->TryGetBoolField(OptedInFieldName, OutOptedIn);
}
