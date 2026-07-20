#pragma once

#include "CoreMinimal.h"
#include "Events/PostHogBeforeSend.h"
#include "Events/PostHogExceptionInput.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TimerManager.h"
#include "PostHogRuntimeSubsystem.generated.h"


class UPostHogEventProperties;
class UPostHogEventPropertyArray;
class FPostHogConsentController;
class FPostHogExceptionCapture;
class FPostHogQuitFlushCoordinator;

// Immediate, Blueprint-friendly acceptance result returned by UPostHogRuntimeSubsystem::Flush.
// Reports only whether a drain request was accepted, not how it eventually finished.
UENUM(BlueprintType)
enum class EPostHogFlushRequestResult : uint8
{
	Started,
	AlreadyInProgress,
	Skipped
};

// Eventual outcome of a manual flush, delivered to a bound FPostHogFlushCompletedDelegate.
// Mirrors the internal queue drain result, kept as a separate public type so callers are
// insulated from internal queue implementation changes.
UENUM(BlueprintType)
enum class EPostHogFlushOutcome : uint8
{
	Drained,
	Empty,
	Failed,
	Cancelled,
	ProgressBlocked,
	Paused,
	SkippedOffline
};

// C++-only completion notification for a manual Flush() call; not Blueprint-exposed since
// delegates with payloads are not natively assignable from Blueprint graphs.
DECLARE_DELEGATE_OneParam(FPostHogFlushCompletedDelegate, EPostHogFlushOutcome);

/**
 *
 */
UCLASS()
class UNREALHOG_API UPostHogRuntimeSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UPostHogRuntimeSubsystem();
	// We need a special constructor to preserve using Forward Declared TUniquePtrs.
	// We could have opted to use TPimplPtrs, but wanted to preserve the functionality of the TUniqutePtrs.
	// This trick is used in-engine as well, in UChaosClothAsset.
	// Engine/Plugins/ChaosClothAsset/Source/ChaosClothAssetEngine/Public/ChaosClothAsset/ClothAsset.h
	UPostHogRuntimeSubsystem(FVTableHelper& Helper);
	virtual ~UPostHogRuntimeSubsystem() override;

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category="PostHog|Events")
	void CaptureEvent(const FString& EventName, UPostHogEventProperties* Properties = nullptr);

	UFUNCTION(BlueprintCallable, Category="PostHog|Events")
	void CaptureException(const FPostHogExceptionInput& Exception, UPostHogEventProperties* Properties = nullptr);
  
  UFUNCTION(BlueprintCallable, Category="PostHog|Events")
	void CaptureScreen(const FString& ScreenName, UPostHogEventProperties* Properties = nullptr);

	UFUNCTION(BlueprintCallable, Category="PostHog|Events")
	UPostHogEventProperties* CreateEventProperties();

	UFUNCTION(BlueprintCallable, Category="PostHog|Events")
	UPostHogEventPropertyArray* CreateEventPropertyArray();

	// Requests a complete asynchronous drain of the queued events. Returns immediately with
	// acceptance status; does not block Blueprint or the game thread. Safe to call before
	// consent, before Initialize(), during/after Deinitialize(), or with an empty queue.
	UFUNCTION(BlueprintCallable, Category="PostHog|Events")
	EPostHogFlushRequestResult Flush();

	// C++-only overload that additionally reports the eventual drain outcome via OnComplete,
	// invoked exactly once (synchronously for a Skipped result, otherwise when the shared
	// drain completes).
	EPostHogFlushRequestResult Flush(FPostHogFlushCompletedDelegate OnComplete);

#if WITH_DEV_AUTOMATION_TESTS
	void SetConsentControllerForTests(TUniquePtr<FPostHogConsentController> InConsentController);
	void ResetConsentControllerForTests();
	int32 GetQueuedEventCountForTests() const;
#endif

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

	// Bounded drain-then-exit: performs the same flush/timeout/shutdown sequence as the
	// bFlushOnQuit-gated window-close veto, then requests engine exit. Not gated by bFlushOnQuit
	// since this is an explicit developer action, and it covers programmatic quit paths
	// (UKismetSystemLibrary::QuitGame, the `quit` console command) that bypass the veto entirely.
	// Safe to call alongside an in-progress vetoed close: only the first caller to reach the
	// coordinator starts the flush, and engine exit is still requested exactly once.
	UFUNCTION(BlueprintCallable, Category="PostHog|Events")
	void FlushAndQuit();

private:
	EPostHogFlushRequestResult RequestFlushInternal(FPostHogFlushCompletedDelegate OnComplete);
	void FlushQueuedEvents();
	void StartFlushTimer();
	void StopFlushTimer();
	void UpdateExceptionCaptureRegistration();

	// FCoreDelegates::OnEnginePreExit handler: storage-only finalize, no coordinator involvement
	// and no network I/O, since the engine may already be tearing down by this point.
	void HandleEnginePreExit();

	// UGameViewportClient::OnWindowCloseRequested() handler, bound only when bFlushOnQuit is
	// enabled. Always vetoes the close (returns false); the coordinator itself requests engine
	// exit once the bounded drain completes or times out.
	bool HandleWindowCloseRequested();

	FTimerHandle FlushTimerHandle;

	TUniquePtr<FPostHogConsentController> ConsentController;
	TUniquePtr<FPostHogExceptionCapture> ExceptionCapture;
	TUniquePtr<FPostHogQuitFlushCoordinator> QuitCoordinator;
	FDelegateHandle OnEnginePreExitHandle;
};
