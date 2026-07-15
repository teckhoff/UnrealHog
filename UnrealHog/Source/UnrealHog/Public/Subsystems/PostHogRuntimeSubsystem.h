// Trevor Eckhoff, 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TimerManager.h"
#include "PostHogRuntimeSubsystem.generated.h"


class UPostHogEventProperties;
class UPostHogEventPropertyArray;
class FPostHogConsentController;
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

	UFUNCTION(BlueprintCallable, Category="PostHog|Events")
	UPostHogEventPropertyArray* CreateEventPropertyArray();

	UFUNCTION(BlueprintCallable, Category="PostHog|Events")
	void Flush();

	UFUNCTION(BlueprintCallable, Category="PostHog|Consent")
	void SetAnalyticsOptIn(bool bOptIn);

	UFUNCTION(BlueprintPure, Category="PostHog|Consent")
	bool IsAnalyticsOptedIn() const;

private:
	void FlushQueuedEvents();
	void StartFlushTimer();
	void StopFlushTimer();

	FTimerHandle FlushTimerHandle;

	TUniquePtr<FPostHogConsentController> ConsentController;
};
