// Trevor Eckhoff, 2026. All rights reserved.

#include "Subsystems/PostHogRuntimeSubsystem.h"

#include "PostHogDeveloperSettings.h"
#include "Dom/JsonObject.h"
#include "Events/PostHogBatchPayload.h"
#include "Events/PostHogEvent.h"
#include "Http/PostHogHttpClient.h"
#include "Logging/PostHogLogger.h"
#include "Logging/StructuredLog.h"
#include "SDK/PostHogSdkInfo.h"
#include "Storage/PostHogStorageProvider.h"


bool UPostHogRuntimeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UPostHogDeveloperSettings* Settings = GetDefault<UPostHogDeveloperSettings>();
	
	return Settings->IsAnalyticsEnabled();
}

void UPostHogRuntimeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	
	const UPostHogDeveloperSettings* Settings = GetDefault<UPostHogDeveloperSettings>();
	
	const FString LibraryName = PostHogSdkInfo::GetLibraryName();
	const FString LibraryVersion = PostHogSdkInfo::GetPluginVersion();
	const FString UserAgent = PostHogSdkInfo::GetUserAgent();
	
	StorageProvider = IPostHogStorageProvider::CreateDefaultProvider();
	
	SessionId = FGuid::NewGuid();
	
	UE_LOGFMT(LogPostHog, Log, "PostHog Runtime Subsystem Initialized. Plugin {LibraryName} (ver. {Version}) ({Agent})", LibraryName, LibraryVersion, UserAgent);
	
	FPostHogHttpClient* HttpClient = new FPostHogHttpClient(Settings->GetResolvedHost());
	
	FPostHogEvent Event(TEXT("login"), SessionId.ToString(EGuidFormats::DigitsWithHyphensLower));
	Event.SetProcessPersonProfile(false);
	StorageProvider->SaveEvent(Event.GetEventId(), Event.ToJsonObject());
	
	FPostHogEvent Event2(TEXT("started_game"), SessionId.ToString(EGuidFormats::DigitsWithHyphensLower));
	Event2.SetProcessPersonProfile(false);
	Event2.SetNumberProperty(TEXT("money"), 10000);
	StorageProvider->SaveEvent(Event2.GetEventId(), Event2.ToJsonObject());
	
	FPostHogBatchPayload Payload(Settings->GetApiKey());
	Payload.AddEvent(Event);
	Payload.AddEvent(Event2);
	
	HttpClient->SendBatch(Payload, [](bool bSuccess, int32 StatusCode, const FString& ResponseBody)
	{
		UE_LOGFMT(LogPostHog, Log, "PostHog Event Sent. Status Code: {StatusCode}, Response Body: {ResponseBody}", StatusCode, ResponseBody);
	});
}
