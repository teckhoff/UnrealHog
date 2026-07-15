// Trevor Eckhoff, 2026. All rights reserved.

#include "Consent/PostHogConsentController.h"

#include "Dom/JsonObject.h"
#include "Events/PostHogEvent.h"
#include "Events/PostHogEventQueue.h"
#include "Http/PostHogBatchTransport.h"
#include "Logging/PostHogLogger.h"
#include "Logging/StructuredLog.h"
#include "PostHogDeveloperSettings.h"
#include "PostHogSettingsValidation.h"
#include "Serialization/JsonSerializer.h"
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
		UE_LOGFMT(LogPostHog, Warning, "PostHog Consent Controller rejected opt-in ({Reason}).", FailureReason);
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

	SessionId = UuidGenerator();
	++SessionCreationCount;

	if (SessionId.IsEmpty())
	{
		OutFailureReason = TEXT("failed to generate session identifier");
		StorageProvider.Reset();
		return false;
	}

	Transport = TransportFactory(ValidationResult.ResolvedHost);
	++TransportCreationCount;

	EventQueue = MakeUnique<FPostHogEventQueue>(*StorageProvider, *Transport, Settings.GetApiKey(),
		Settings.GetMaxQueueSize(), Settings.GetMaxBatchSize(), Settings.GetFlushEventCount());

	bIsOptedIn = true;
	PersistOptIn(true);

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
	SessionId.Empty();
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
