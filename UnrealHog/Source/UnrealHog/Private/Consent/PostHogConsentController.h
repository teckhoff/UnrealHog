#pragma once

#include "CoreMinimal.h"
#include "Events/PostHogBeforeSend.h"
#include "Lifecycle/PostHogApplicationLifecycleHandler.h"
#include "Templates/Function.h"

class IPostHogStorageProvider;
class IPostHogBatchTransport;
class FPostHogEventQueue;
class FPostHogApplicationLifecycleHandler;
class FPostHogIdentityManager;
class FPostHogSessionManager;
class FPostHogSuperPropertiesManager;
class UPostHogDeveloperSettings;
class UPostHogEventProperties;
struct FPostHogEvent;
struct FPostHogEventProperty;

// Outcome of FPostHogConsentController::CaptureEvent, observable by callers and tests.
enum class EPostHogCaptureResult : uint8
{
	Success,
	InvalidEventName,
	NotOptedIn,
	DroppedByBeforeSend,
	BeforeSendFailed,
	EnqueueFailed
};

/**
 * @brief Owns the opt-in/opt-out lifecycle and the runtime collaborators (storage, transport,
 * event queue, session id) that must not exist until analytics collection is permitted.
 *
 * Not a UObject: constructed and owned by UPostHogRuntimeSubsystem, but kept free of engine
 * subsystem lifetime concerns so it can be exercised directly in automation tests with fake
 * collaborator factories.
 */
class FPostHogConsentController
{
public:
	using FStorageProviderFactory = TFunction<TUniquePtr<IPostHogStorageProvider>()>;
	using FTransportFactory = TFunction<TUniquePtr<IPostHogBatchTransport>(const FString& ResolvedHost)>;
	using FUuidGenerator = TFunction<FString()>;
	using FLifecycleMetadataProvider = FPostHogApplicationLifecycleHandler::FMetadataProvider;

	FPostHogConsentController(FStorageProviderFactory InStorageProviderFactory,
		FTransportFactory InTransportFactory,
		FUuidGenerator InUuidGenerator,
		FLifecycleMetadataProvider InLifecycleMetadataProvider = nullptr);
	~FPostHogConsentController();

	// Loads persisted opt-in state (falling back to the settings default) and, if opted in,
	// validates settings and lazily creates runtime collaborators. Side-effect free when opted out.
	void Initialize(const UPostHogDeveloperSettings& Settings);

	// Cancels any in-flight delivery without altering opt-in state or queued events.
	void Shutdown();

	// Idempotent. Opting in validates settings and lazily creates collaborators on failure remains
	// opted out and returns false. Opting out blocks capture, clears the queue, and releases collaborators.
	bool SetOptIn(bool bOptIn, const UPostHogDeveloperSettings& Settings);

	bool IsOptedIn() const { return bIsOptedIn; }

	void SetBeforeSend(FPostHogBeforeSendDelegate InBeforeSend);
	void ClearBeforeSend();

	bool Capture(const FPostHogEvent& Event);

	// Single producer path for composing a capture: validates the event name, then layers
	// persisted super properties, call properties, SDK-owned properties, session id, and groups
	// (in that precedence order) before enqueuing via Capture(). Reserved keys in CallProperties
	// are stripped by UPostHogEventProperties::ApplyToEvent, never overriding SDK-owned values.
	EPostHogCaptureResult CaptureEvent(const FString& EventName, UPostHogEventProperties* CallProperties, bool bProcessPersonProfile);

	// Blank DistinctId is a safe no-op (no event, no state mutation). Persists the new
	// identity first, then emits $identify via CaptureEvent so distinct_id reflects it;
	// $anon_distinct_id is included only on first identification.
	EPostHogCaptureResult Identify(const FString& DistinctId, UPostHogEventProperties* UserProperties, UPostHogEventProperties* UserPropertiesSetOnce);

	// Returns to anonymous state: regenerates or reuses AnonymousId per
	// Settings.ShouldReuseAnonymousId(), clears groups, and starts a new session. No event
	// emitted. No-op when not opted in.
	void Reset(const UPostHogDeveloperSettings& Settings);

	// Blank Alias is a safe no-op (no event).
	EPostHogCaptureResult Alias(const FString& Alias);

	// Registers Key=Value as a persisted super property applied to every future event ahead of
	// call and SDK-owned properties. Returns false (safe no-op) when not opted in.
	bool RegisterSuperProperty(const FString& Key, const FPostHogEventProperty& Value);

	// Removes a previously registered super property. Safe no-op when not opted in.
	void UnregisterSuperProperty(const FString& Key);

	// Removes all registered super properties. Safe no-op when not opted in.
	void ClearSuperProperties();

	void Flush();

	// Rotating in-memory session id, independent of the persistent distinct id. Never
	// generated before collection is permitted.
	FString GetSessionId();

	// Effective distinct id (identified id if known, else the persistent anonymous id).
	// Empty when collection is not permitted.
	const FString& GetDistinctId() const;
	FPostHogEventQueue* GetEventQueue() const { return EventQueue.Get(); }
	int32 GetQueuedEventCount() const;

	int32 GetStorageProviderCreationCount() const { return StorageProviderCreationCount; }
	int32 GetTransportCreationCount() const { return TransportCreationCount; }

	// Number of times EnableCollection has loaded or created the identity manager (once per
	// successful opt-in transition), not the number of distinct anonymous ids generated: the
	// persisted anonymous id is reused across a disable/enable cycle against the same storage.
	int32 GetIdentityManagerLoadCount() const { return IdentityManagerLoadCount; }

	// Forwards application foreground/background transitions to the session manager.
	void NotifyApplicationForegrounded();
	void NotifyApplicationBackgrounded();

private:
	bool EnableCollection(const UPostHogDeveloperSettings& Settings, FString& OutFailureReason);
	void DisableCollection();

	void PersistOptIn(bool bOptIn) const;
	static bool TryLoadPersistedOptIn(IPostHogStorageProvider& StorageProvider, bool& OutOptedIn);

	void ApplySuperProperties(FPostHogEvent& Event) const;

	// Extension point for a future EP. No-op today: EP-004 does not implement group storage, but
	// CaptureEvent's precedence order already reserves its slot.
	void ApplyGroups(FPostHogEvent& Event) const;

	FStorageProviderFactory StorageProviderFactory;
	FTransportFactory TransportFactory;
	FUuidGenerator UuidGenerator;
	FLifecycleMetadataProvider LifecycleMetadataProvider;
	FPostHogBeforeSendDelegate BeforeSend;

	bool bIsOptedIn = false;

	TUniquePtr<IPostHogStorageProvider> StorageProvider;
	TUniquePtr<IPostHogBatchTransport> Transport;
	TUniquePtr<FPostHogEventQueue> EventQueue;
	TUniquePtr<FPostHogIdentityManager> IdentityManager;
	TUniquePtr<FPostHogSuperPropertiesManager> SuperPropertiesManager;
	TUniquePtr<FPostHogSessionManager> SessionManager;
	TUniquePtr<FPostHogApplicationLifecycleHandler> LifecycleHandler;

	int32 StorageProviderCreationCount = 0;
	int32 TransportCreationCount = 0;
	int32 IdentityManagerLoadCount = 0;
};
