#include "Consent/PostHogConsentController.h"

#include "Dom/JsonObject.h"
#include "Events/PostHogCapturePolicy.h"
#include "Events/PostHogEvent.h"
#include "Events/PostHogEventProperties.h"
#include "Events/PostHogEventQueue.h"
#include "Http/PostHogBatchTransport.h"
#include "Logging/PostHogLogger.h"
#include "Logging/StructuredLog.h"
#include "PostHogDeveloperSettings.h"
#include "PostHogSettingsValidation.h"
#include "Serialization/JsonSerializer.h"
#include "Session/PostHogSessionManager.h"
#include "Storage/PostHogStorageProvider.h"

namespace
{
	const FString OptInStateKey = TEXT("opt_in_status");
	const FString OptedInFieldName = TEXT("opted_in");
}

FPostHogConsentController::FPostHogConsentController(FStorageProviderFactory InStorageProviderFactory,
	FTransportFactory InTransportFactory, FUuidGenerator InUuidGenerator) :
	StorageProviderFactory(MoveTemp(InStorageProviderFactory)),
	TransportFactory(MoveTemp(InTransportFactory)),
	UuidGenerator(MoveTemp(InUuidGenerator))
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
		UE_LOGFMT(LogPostHog, Warning, "PostHog Consent Controller could not restore opt-in state ({Reason}); remaining opted out.", FailureReason);
		StorageProvider.Reset();
	}
}

void FPostHogConsentController::Shutdown()
{
	if (EventQueue)
	{
		EventQueue->CancelInFlightRequest();
	}
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
#if !WITH_DEV_AUTOMATION_TESTS
		UE_LOGFMT(LogPostHog, Warning, "PostHog Consent Controller rejected opt-in ({Reason}).", FailureReason);
#endif
	}
	return bOk;
}

bool FPostHogConsentController::Capture(const FPostHogEvent& Event)
{
	if (!bIsOptedIn || !EventQueue.IsValid())
	{
		return false;
	}

	return EventQueue->Enqueue(Event);
}

void FPostHogConsentController::SetBeforeSend(FPostHogBeforeSendDelegate InBeforeSend)
{
	BeforeSend = MoveTemp(InBeforeSend);
}

void FPostHogConsentController::ClearBeforeSend()
{
	BeforeSend.Unbind();
}

EPostHogCaptureResult FPostHogConsentController::CaptureEvent(const FString& EventName, UPostHogEventProperties* CallProperties, bool bProcessPersonProfile)
{
	if (!PostHogCapturePolicy::IsValidEventName(EventName))
	{
#if !WITH_DEV_AUTOMATION_TESTS
		UE_LOGFMT(LogPostHog, Warning, "PostHog Consent Controller rejected capture with an empty or whitespace-only event name.");
#endif
		return EPostHogCaptureResult::InvalidEventName;
	}

	if (!bIsOptedIn || !EventQueue.IsValid())
	{
#if !WITH_DEV_AUTOMATION_TESTS
		UE_LOGFMT(LogPostHog, Warning, "PostHog Consent Controller has no analytics consent; dropping event {EventName}.", EventName);
#endif
		return EPostHogCaptureResult::NotOptedIn;
	}

	FPostHogEvent GeneratedEvent(EventName, DistinctId);

	ApplySuperProperties(GeneratedEvent);

	if (CallProperties)
	{
		CallProperties->ApplyToEvent(GeneratedEvent);
	}

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
		UE_LOGFMT(LogPostHog, Error, "PostHog before-send callback reported failure for event {EventName}; dropping event before persistence.", EventName);
		return EPostHogCaptureResult::BeforeSendFailed;
	}

	if (!Capture(GeneratedEvent))
	{
		UE_LOGFMT(LogPostHog, Error, "PostHog Consent Controller failed to enqueue event {EventName}.", EventName);
		return EPostHogCaptureResult::EnqueueFailed;
	}

	SessionManager->Touch();

	return EPostHogCaptureResult::Success;
}

void FPostHogConsentController::ApplySuperProperties(FPostHogEvent& Event) const
{
	// EP-004 does not implement super-property storage; a later EP populates persisted
	// super properties here, ahead of call/SDK/session/group precedence.
}

void FPostHogConsentController::ApplyGroups(FPostHogEvent& Event) const
{
	// EP-004 does not implement group storage; a later EP populates persisted group
	// associations here, after SDK/session precedence is applied.
}

void FPostHogConsentController::Flush()
{
	if (EventQueue.IsValid())
	{
		EventQueue->Flush();
	}
}

int32 FPostHogConsentController::GetQueuedEventCount() const
{
	return EventQueue.IsValid() ? EventQueue->Num() : 0;
}

FString FPostHogConsentController::GetSessionId()
{
	return SessionManager->GetSessionId();
}

void FPostHogConsentController::NotifyApplicationForegrounded()
{
	SessionManager->OnForeground();
}

void FPostHogConsentController::NotifyApplicationBackgrounded()
{
	SessionManager->OnBackground();
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

	if (!StorageProvider.IsValid())
	{
		StorageProvider = StorageProviderFactory();
		++StorageProviderCreationCount;
	}

	DistinctId = UuidGenerator();
	++DistinctIdCreationCount;

	if (DistinctId.IsEmpty())
	{
		OutFailureReason = TEXT("failed to generate distinct identifier");
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

	return true;
}

void FPostHogConsentController::DisableCollection()
{
	bIsOptedIn = false;

	if (EventQueue)
	{
		EventQueue->Clear();
	}

	PersistOptIn(false);

	EventQueue.Reset();
	Transport.Reset();
	StorageProvider.Reset();
	DistinctId.Empty();
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
