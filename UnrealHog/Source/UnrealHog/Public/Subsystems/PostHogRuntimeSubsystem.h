// Trevor Eckhoff, 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TimerManager.h"
#include "PostHogRuntimeSubsystem.generated.h"


class IPostHogStorageProvider;
class FPostHogHttpClient;
class FPostHogEventQueue;
class UPostHogEventProperties;
/**
 * 
 */
UCLASS()
class UNREALHOG_API UPostHogRuntimeSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
	
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	virtual void Deinitialize() override;
	
	UFUNCTION(BlueprintCallable, Category="PostHog|Events")
	void CaptureEvent(const FString& EventName, UPostHogEventProperties* Properties = nullptr);
	
	UFUNCTION(BlueprintCallable, Category="PostHog|Events")
	UPostHogEventProperties* CreateEventProperties();
	
private:
	void FlushQueuedEvents();
	
	FString SessionId;
	FTimerHandle FlushTimerHandle;
	
	TUniquePtr<IPostHogStorageProvider> StorageProvider;
	TUniquePtr<FPostHogHttpClient> HttpClient;
	TUniquePtr<FPostHogEventQueue> EventQueue;
};
