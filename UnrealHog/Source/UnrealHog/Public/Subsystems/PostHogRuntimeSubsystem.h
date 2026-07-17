#pragma once

#include "CoreMinimal.h"
#include "Events/PostHogBeforeSend.h"
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

	UFUNCTION(BlueprintCallable, Category="PostHog|Identity")
	void Identify(const FString& DistinctId, UPostHogEventProperties* UserProperties = nullptr, UPostHogEventProperties* UserPropertiesSetOnce = nullptr);

	UFUNCTION(BlueprintCallable, Category="PostHog|Identity")
	void Reset();

	UFUNCTION(BlueprintCallable, Category="PostHog|Identity")
	void Alias(const FString& AliasId);

	UFUNCTION(BlueprintPure, Category="PostHog|Identity")
	FString GetDistinctId() const;

	UFUNCTION(BlueprintCallable, Category="PostHog|Groups")
	void Group(const FString& GroupType, const FString& GroupKey, UPostHogEventProperties* GroupProperties = nullptr);

	UFUNCTION(BlueprintCallable, Category="PostHog|Groups")
	void ResetGroups();

	UFUNCTION(BlueprintCallable, Category="PostHog|SuperProperties")
	void RegisterSuperPropertyString(const FString& Key, const FString& StringValue);

	UFUNCTION(BlueprintCallable, Category="PostHog|SuperProperties")
	void RegisterSuperPropertyNumber(const FString& Key, double NumberValue);

	UFUNCTION(BlueprintCallable, Category="PostHog|SuperProperties")
	void RegisterSuperPropertyBoolean(const FString& Key, bool bBoolValue);

	UFUNCTION(BlueprintCallable, Category="PostHog|SuperProperties")
	void RegisterSuperPropertyNull(const FString& Key);

	UFUNCTION(BlueprintCallable, Category="PostHog|SuperProperties")
	void RegisterSuperPropertyObject(const FString& Key, UPostHogEventProperties* Value);

	UFUNCTION(BlueprintCallable, Category="PostHog|SuperProperties")
	void RegisterSuperPropertyArray(const FString& Key, UPostHogEventPropertyArray* Value);

	UFUNCTION(BlueprintCallable, Category="PostHog|SuperProperties")
	void UnregisterSuperProperty(const FString& Key);

	UFUNCTION(BlueprintCallable, Category="PostHog|SuperProperties")
	void ClearSuperProperties();

	void SetBeforeSend(FPostHogBeforeSendDelegate InBeforeSend);
	void ClearBeforeSend();

private:
	void FlushQueuedEvents();
	void StartFlushTimer();
	void StopFlushTimer();

	FTimerHandle FlushTimerHandle;

	TUniquePtr<FPostHogConsentController> ConsentController;
};
