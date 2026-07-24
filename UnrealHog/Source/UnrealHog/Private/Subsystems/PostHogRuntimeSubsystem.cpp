#include "Subsystems/PostHogRuntimeSubsystem.h"

#include "Consent/PostHogConsentController.h"
#include "ErrorTracking/PostHogExceptionCapture.h"
#include "PostHogDeveloperSettings.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Http/PostHogHttpClient.h"
#include "Lifecycle/PostHogQuitFlushCoordinator.h"
#include "Logging/PostHogLogger.h"
#include "Logging/StructuredLog.h"
#include "Misc/CoreDelegates.h"
#include "SDK/PostHogSdkInfo.h"
#include "Storage/PostHogStorageProvider.h"
#include "Subsystems/PostHogRuntimeSubsystemFlushOutcome.h"
#include "Events/PostHogEventProperties.h"
#include "TimerManager.h"
#include "Utilities/PostHogUuidV7.h"

namespace
{
	EPostHogFlushRequestResult TranslateFlushRequestResult(EPostHogConsentFlushRequestResult Result)
	{
		switch (Result)
		{
		case EPostHogConsentFlushRequestResult::Started:
			return EPostHogFlushRequestResult::Started;
		case EPostHogConsentFlushRequestResult::AlreadyInProgress:
			return EPostHogFlushRequestResult::AlreadyInProgress;
		case EPostHogConsentFlushRequestResult::Skipped:
		default:
			return EPostHogFlushRequestResult::Skipped;
		}
	}
}

UPostHogRuntimeSubsystem::UPostHogRuntimeSubsystem() = default;

UPostHogRuntimeSubsystem::UPostHogRuntimeSubsystem(FVTableHelper& Helper)
	: Super(Helper)
{
}

UPostHogRuntimeSubsystem::~UPostHogRuntimeSubsystem() = default;

bool UPostHogRuntimeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// The subsystem must always exist so consent can be granted at runtime even when analytics
	// is currently disabled or misconfigured; CaptureEvent/Flush remain safe no-ops until opt-in.
	return Super::ShouldCreateSubsystem(Outer);
}

void UPostHogRuntimeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UPostHogDeveloperSettings* Settings = GetDefault<UPostHogDeveloperSettings>();

	// Apply the configured SDK log level before any collaborator (consent restoration, settings
	// validation, storage, lifecycle, exception capture, or transport) can emit a diagnostic. Every
	// Initialize reapplies the project setting, so a runtime console override is deterministically
	// restored on the next subsystem initialization.
	PostHogLogger::ApplyConfiguredLevel(Settings->GetLogLevel());

	ConsentController = MakeUnique<FPostHogConsentController>(
		[]() { return IPostHogStorageProvider::CreateDefaultProvider(); },
		[](const FString& Host) -> TUniquePtr<IPostHogBatchTransport> { return MakeUnique<FPostHogHttpClient>(Host); },
		[]() { return PostHogUuidV7::New(); });

	ConsentController->Initialize(*Settings);

	ExceptionCapture = MakeUnique<FPostHogExceptionCapture>(*ConsentController);
	UpdateExceptionCaptureRegistration();

	QuitCoordinator = MakeUnique<FPostHogQuitFlushCoordinator>(
		[this](FPostHogEventQueueFlushComplete OnComplete) { ConsentController->RequestFlush(MoveTemp(OnComplete)); },
		[this]() { ConsentController->Shutdown(); },
		Settings->GetFlushOnQuitTimeoutSeconds());

	OnEnginePreExitHandle = FCoreDelegates::OnEnginePreExit.AddUObject(this, &UPostHogRuntimeSubsystem::HandleEnginePreExit);

	if (Settings->ShouldFlushOnQuit())
	{
		if (UGameInstance* OwningGameInstance = GetGameInstance())
		{
			if (UGameViewportClient* ViewportClient = OwningGameInstance->GetGameViewportClient())
			{
				ViewportClient->OnWindowCloseRequested().BindUObject(this, &UPostHogRuntimeSubsystem::HandleWindowCloseRequested);
			}
		}
	}

	if (ConsentController->IsOptedIn())
	{
		const FString LibraryName = FPostHogSdkInfo::GetLibraryName();
		const FString LibraryVersion = FPostHogSdkInfo::GetPluginVersion();
		const FString UserAgent = FPostHogSdkInfo::GetUserAgent();

		UE_LOGFMT(LogUnrealHog, Log, "PostHog Runtime Subsystem Initialized with active consent. Plugin {LibraryName} (ver. {Version}) ({Agent})", LibraryName, LibraryVersion, UserAgent);

		StartFlushTimer();
	}
	else
	{
		UE_LOGFMT(LogUnrealHog, Log, "PostHog Runtime Subsystem Initialized without consent; collection remains disabled until opt-in.");
	}
}

void UPostHogRuntimeSubsystem::Deinitialize()
{
	StopFlushTimer();

	if (OnEnginePreExitHandle.IsValid())
	{
		FCoreDelegates::OnEnginePreExit.Remove(OnEnginePreExitHandle);
		OnEnginePreExitHandle.Reset();
	}

	if (UGameInstance* OwningGameInstance = GetGameInstance())
	{
		if (UGameViewportClient* ViewportClient = OwningGameInstance->GetGameViewportClient())
		{
			// Only unbind our own handler: OnWindowCloseRequested is single-bind, so clobbering
			// a different (e.g. game code's) handler here would silently break it.
			if (ViewportClient->OnWindowCloseRequested().IsBoundToObject(this))
			{
				ViewportClient->OnWindowCloseRequested().Unbind();
			}
		}
	}

	// Destroying the coordinator invokes any still-pending timeout cancel closure; it never
	// initiates network I/O itself.
	QuitCoordinator.Reset();

	if (ExceptionCapture)
	{
		ExceptionCapture->UnregisterHandlers();
		ExceptionCapture.Reset();
	}

	if (ConsentController)
	{
		// Storage-only finalize: Deinitialize must never initiate network I/O.
		ConsentController->Shutdown();
	}

	Super::Deinitialize();
}

void UPostHogRuntimeSubsystem::HandleEnginePreExit()
{
	if (ConsentController)
	{
		// Storage-only finalize: the engine may already be tearing down, so no network I/O and
		// no coordinator involvement here.
		ConsentController->Shutdown();
	}
}

bool UPostHogRuntimeSubsystem::HandleWindowCloseRequested()
{
	if (QuitCoordinator)
	{
		QuitCoordinator->BeginFlushAndQuit();
	}

	// Always veto: the coordinator itself requests engine exit once the bounded drain completes
	// or times out.
	return false;
}

void UPostHogRuntimeSubsystem::FlushAndQuit()
{
	if (QuitCoordinator)
	{
		QuitCoordinator->BeginFlushAndQuit();
	}
}

void UPostHogRuntimeSubsystem::CaptureEvent(const FString& EventName, UPostHogEventProperties* Properties)
{
	if (!ConsentController)
	{
		UE_LOGFMT(LogUnrealHog, Verbose, "PostHog Runtime Subsystem has no analytics consent; dropping event {EventName}.", EventName);
		return;
	}

	const EPostHogCaptureResult Result = ConsentController->CaptureEvent(EventName, Properties);

	switch (Result)
	{
	case EPostHogCaptureResult::InvalidEventName:
		UE_LOGFMT(LogUnrealHog, Warning, "PostHog Runtime Subsystem rejected an empty or whitespace-only event name.");
		break;
	case EPostHogCaptureResult::NotOptedIn:
		UE_LOGFMT(LogUnrealHog, Verbose, "PostHog Runtime Subsystem has no analytics consent; dropping event {EventName}.", EventName);
		break;
	case EPostHogCaptureResult::DroppedByBeforeSend:
	case EPostHogCaptureResult::BeforeSendFailed:
		break;
	case EPostHogCaptureResult::EnqueueFailed:
		UE_LOGFMT(LogUnrealHog, Error, "PostHog Runtime Subsystem failed to enqueue event {EventName}.", EventName);
		break;
	case EPostHogCaptureResult::Success:
	default:
		break;
	}
}

void UPostHogRuntimeSubsystem::CaptureException(const FPostHogExceptionInput& Exception, UPostHogEventProperties* Properties)
{
	if (!ConsentController)
	{
		return;
	}

	ConsentController->CaptureException(Exception, Properties);
}

void UPostHogRuntimeSubsystem::CaptureScreen(const FString& ScreenName, UPostHogEventProperties* Properties)
{
	if (!ConsentController)
	{
		UE_LOGFMT(LogUnrealHog, Verbose, "PostHog Runtime Subsystem has no analytics consent; dropping screen {ScreenName}.", ScreenName);
		return;
	}

	const EPostHogCaptureResult Result = ConsentController->CaptureScreen(ScreenName, Properties);

	switch (Result)
	{
	case EPostHogCaptureResult::InvalidEventName:
		UE_LOGFMT(LogUnrealHog, Warning, "PostHog Runtime Subsystem rejected an empty or whitespace-only screen name.");
		break;
	case EPostHogCaptureResult::NotOptedIn:
		UE_LOGFMT(LogUnrealHog, Verbose, "PostHog Runtime Subsystem has no analytics consent; dropping screen {ScreenName}.", ScreenName);
		break;
	case EPostHogCaptureResult::DroppedByBeforeSend:
	case EPostHogCaptureResult::BeforeSendFailed:
		break;
	case EPostHogCaptureResult::EnqueueFailed:
		UE_LOGFMT(LogUnrealHog, Error, "PostHog Runtime Subsystem failed to enqueue screen {ScreenName}.", ScreenName);
		break;
	case EPostHogCaptureResult::Success:
	default:
		break;
	}
}

UPostHogEventProperties* UPostHogRuntimeSubsystem::CreateEventProperties()
{
	return NewObject<UPostHogEventProperties>(this);
}

UPostHogEventPropertyArray* UPostHogRuntimeSubsystem::CreateEventPropertyArray()
{
	return NewObject<UPostHogEventPropertyArray>(this);
}

EPostHogFlushRequestResult UPostHogRuntimeSubsystem::Flush()
{
	return RequestFlushInternal({});
}

EPostHogFlushRequestResult UPostHogRuntimeSubsystem::Flush(FPostHogFlushCompletedDelegate OnComplete)
{
	return RequestFlushInternal(MoveTemp(OnComplete));
}

#if WITH_DEV_AUTOMATION_TESTS
void UPostHogRuntimeSubsystem::SetConsentControllerForTests(TUniquePtr<FPostHogConsentController> InConsentController)
{
	ConsentController = MoveTemp(InConsentController);
}

void UPostHogRuntimeSubsystem::ResetConsentControllerForTests()
{
	ConsentController.Reset();
}

int32 UPostHogRuntimeSubsystem::GetQueuedEventCountForTests() const
{
	return ConsentController ? ConsentController->GetQueuedEventCount() : 0;
}
#endif

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

	UpdateExceptionCaptureRegistration();
}

bool UPostHogRuntimeSubsystem::IsAnalyticsOptedIn() const
{
	return ConsentController && ConsentController->IsOptedIn();
}

void UPostHogRuntimeSubsystem::Identify(const FString& DistinctId, UPostHogEventProperties* UserProperties, UPostHogEventProperties* UserPropertiesSetOnce)
{
	if (!ConsentController)
	{
		return;
	}

	ConsentController->Identify(DistinctId, UserProperties, UserPropertiesSetOnce);
}

void UPostHogRuntimeSubsystem::Reset()
{
	if (!ConsentController)
	{
		return;
	}

	ConsentController->Reset(*GetDefault<UPostHogDeveloperSettings>());
}

void UPostHogRuntimeSubsystem::Alias(const FString& AliasId)
{
	if (!ConsentController)
	{
		return;
	}

	ConsentController->Alias(AliasId);
}

FString UPostHogRuntimeSubsystem::GetDistinctId() const
{
	return ConsentController ? ConsentController->GetDistinctId() : FString();
}

void UPostHogRuntimeSubsystem::Group(const FString& GroupType, const FString& GroupKey, UPostHogEventProperties* GroupProperties)
{
	if (!ConsentController)
	{
		return;
	}

	ConsentController->Group(GroupType, GroupKey, GroupProperties);
}

void UPostHogRuntimeSubsystem::ResetGroups()
{
	if (!ConsentController)
	{
		return;
	}

	ConsentController->ResetGroups();
}

void UPostHogRuntimeSubsystem::RegisterSuperPropertyString(const FString& Key, const FString& StringValue)
{
	if (!ConsentController)
	{
		return;
	}

	FPostHogEventProperty Property;
	Property.Type = EPostHogPropertyType::String;
	Property.StringValue = StringValue;

	ConsentController->RegisterSuperProperty(Key, Property);
}

void UPostHogRuntimeSubsystem::RegisterSuperPropertyNumber(const FString& Key, double NumberValue)
{
	if (!ConsentController)
	{
		return;
	}

	FPostHogEventProperty Property;
	Property.Type = EPostHogPropertyType::Number;
	Property.NumberValue = NumberValue;

	ConsentController->RegisterSuperProperty(Key, Property);
}

void UPostHogRuntimeSubsystem::RegisterSuperPropertyBoolean(const FString& Key, bool bBoolValue)
{
	if (!ConsentController)
	{
		return;
	}

	FPostHogEventProperty Property;
	Property.Type = EPostHogPropertyType::Boolean;
	Property.bBoolValue = bBoolValue;

	ConsentController->RegisterSuperProperty(Key, Property);
}

void UPostHogRuntimeSubsystem::RegisterSuperPropertyNull(const FString& Key)
{
	if (!ConsentController)
	{
		return;
	}

	FPostHogEventProperty Property;
	Property.Type = EPostHogPropertyType::Null;

	ConsentController->RegisterSuperProperty(Key, Property);
}

void UPostHogRuntimeSubsystem::RegisterSuperPropertyObject(const FString& Key, UPostHogEventProperties* Value)
{
	if (!ConsentController)
	{
		return;
	}

	FPostHogEventProperty Property;
	Property.Type = EPostHogPropertyType::Object;

	if (Value)
	{
		Property.Children = Value->GetProperties();
	}

	ConsentController->RegisterSuperProperty(Key, Property);
}

void UPostHogRuntimeSubsystem::RegisterSuperPropertyArray(const FString& Key, UPostHogEventPropertyArray* Value)
{
	if (!ConsentController)
	{
		return;
	}

	FPostHogEventProperty Property;
	Property.Type = EPostHogPropertyType::Array;

	if (Value)
	{
		Property.Children = Value->GetElements();
	}

	ConsentController->RegisterSuperProperty(Key, Property);
}

void UPostHogRuntimeSubsystem::UnregisterSuperProperty(const FString& Key)
{
	if (!ConsentController)
	{
		return;
	}

	ConsentController->UnregisterSuperProperty(Key);
}

void UPostHogRuntimeSubsystem::ClearSuperProperties()
{
	if (!ConsentController)
	{
		return;
	}

	ConsentController->ClearSuperProperties();
}

void UPostHogRuntimeSubsystem::SetBeforeSend(FPostHogBeforeSendDelegate InBeforeSend)
{
	if (ConsentController)
	{
		ConsentController->SetBeforeSend(MoveTemp(InBeforeSend));
	}
}

void UPostHogRuntimeSubsystem::ClearBeforeSend()
{
	if (ConsentController)
	{
		ConsentController->ClearBeforeSend();
	}
}

EPostHogFlushRequestResult UPostHogRuntimeSubsystem::RequestFlushInternal(FPostHogFlushCompletedDelegate OnComplete)
{
	if (!ConsentController)
	{
		UE_LOGFMT(LogUnrealHog, Verbose, "PostHog Runtime Subsystem has no analytics consent; skipping flush request.");
		OnComplete.ExecuteIfBound(EPostHogFlushOutcome::Empty);
		return EPostHogFlushRequestResult::Skipped;
	}

	if (ConsentController->IsShuttingDown())
	{
		UE_LOGFMT(LogUnrealHog, Log, "PostHog Runtime Subsystem is shutting down; skipping flush request.");
		OnComplete.ExecuteIfBound(EPostHogFlushOutcome::Empty);
		return EPostHogFlushRequestResult::Skipped;
	}

	if (!ConsentController->IsOptedIn())
	{
		UE_LOGFMT(LogUnrealHog, Log, "PostHog Runtime Subsystem has no analytics consent; skipping flush request.");
		OnComplete.ExecuteIfBound(EPostHogFlushOutcome::Empty);
		return EPostHogFlushRequestResult::Skipped;
	}

	const EPostHogConsentFlushRequestResult Result = ConsentController->RequestFlush(
		[OnComplete](EPostHogEventQueueFlushResult FlushResult)
		{
			OnComplete.ExecuteIfBound(TranslateFlushOutcome(FlushResult));
		});

	return TranslateFlushRequestResult(Result);
}

void UPostHogRuntimeSubsystem::FlushQueuedEvents()
{
	UE_LOGFMT(LogUnrealHog, Log, "Timer Queue Flush!");
	RequestFlushInternal({});
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
	if (!FlushTimerHandle.IsValid())
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FlushTimerHandle);
	}
}

void UPostHogRuntimeSubsystem::UpdateExceptionCaptureRegistration()
{
	if (!ExceptionCapture)
	{
		return;
	}

	const UPostHogDeveloperSettings* Settings = GetDefault<UPostHogDeveloperSettings>();

	if (Settings->ShouldCaptureExceptions() && ConsentController && ConsentController->IsOptedIn())
	{
		UE_LOGFMT(LogUnrealHog, Log, "PostHog automatic exception capture handlers registered.");
		ExceptionCapture->RegisterHandlers(Settings->ShouldCaptureExceptionsInEditor(), Settings->GetExceptionDebounceIntervalMs());
	}
	else
	{
		UE_LOGFMT(LogUnrealHog, Log, "PostHog automatic exception capture inactive (disabled, no consent, or handlers removed).");
		ExceptionCapture->UnregisterHandlers();
	}
}
