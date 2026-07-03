// Trevor Eckhoff, 2026. All rights reserved.

#include "Subsystems/PostHogRuntimeSubsystem.h"

#include "PostHogDeveloperSettings.h"
#include "Logging/PostHogLogger.h"


bool UPostHogRuntimeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UPostHogDeveloperSettings* Settings = GetDefault<UPostHogDeveloperSettings>();
	
	return Settings->IsAnalyticsEnabled();
}

void UPostHogRuntimeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	
	UE_LOG(LogPostHog, Log, TEXT("PostHog Runtime Subsystem Initialized"));
}
