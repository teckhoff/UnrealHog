#include "Subsystems/PostHogRuntimeSubsystem.h"

#include "Consent/PostHogConsentController.h"
#include "PostHogDeveloperSettings.h"
#include "Engine/World.h"
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
		const FString LibraryName = FPostHogSdkInfo::GetLibraryName();
		const FString LibraryVersion = FPostHogSdkInfo::GetPluginVersion();
		const FString UserAgent = FPostHogSdkInfo::GetUserAgent();

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
	if (!ConsentController)
	{
		UE_LOGFMT(LogPostHog, Warning, "PostHog Runtime Subsystem has no analytics consent; dropping event {EventName}.", EventName);
		return;
	}

	const EPostHogCaptureResult Result = ConsentController->CaptureEvent(EventName, Properties);

	switch (Result)
	{
	case EPostHogCaptureResult::InvalidEventName:
		UE_LOGFMT(LogPostHog, Warning, "PostHog Runtime Subsystem rejected an empty or whitespace-only event name.");
		break;
	case EPostHogCaptureResult::NotOptedIn:
		UE_LOGFMT(LogPostHog, Warning, "PostHog Runtime Subsystem has no analytics consent; dropping event {EventName}.", EventName);
		break;
	case EPostHogCaptureResult::DroppedByBeforeSend:
	case EPostHogCaptureResult::BeforeSendFailed:
		break;
	case EPostHogCaptureResult::EnqueueFailed:
		UE_LOGFMT(LogPostHog, Error, "PostHog Runtime Subsystem failed to enqueue event {EventName}.", EventName);
		break;
	case EPostHogCaptureResult::Success:
	default:
		break;
	}
}

void UPostHogRuntimeSubsystem::CaptureScreen(const FString& ScreenName, UPostHogEventProperties* Properties)
{
	if (!ConsentController)
	{
		UE_LOGFMT(LogPostHog, Warning, "PostHog Runtime Subsystem has no analytics consent; dropping screen {ScreenName}.", ScreenName);
		return;
	}

	const EPostHogCaptureResult Result = ConsentController->CaptureScreen(ScreenName, Properties);

	switch (Result)
	{
	case EPostHogCaptureResult::InvalidEventName:
		UE_LOGFMT(LogPostHog, Warning, "PostHog Runtime Subsystem rejected an empty or whitespace-only screen name.");
		break;
	case EPostHogCaptureResult::NotOptedIn:
		UE_LOGFMT(LogPostHog, Warning, "PostHog Runtime Subsystem has no analytics consent; dropping screen {ScreenName}.", ScreenName);
		break;
	case EPostHogCaptureResult::DroppedByBeforeSend:
	case EPostHogCaptureResult::BeforeSendFailed:
		break;
	case EPostHogCaptureResult::EnqueueFailed:
		UE_LOGFMT(LogPostHog, Error, "PostHog Runtime Subsystem failed to enqueue screen {ScreenName}.", ScreenName);
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
	if (!FlushTimerHandle.IsValid())
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FlushTimerHandle);
	}
}
