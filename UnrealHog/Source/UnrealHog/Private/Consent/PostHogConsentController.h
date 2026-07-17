#pragma once

#include "CoreMinimal.h"
#include "Events/PostHogBeforeSend.h"
#include "Events/PostHogExceptionInput.h"
#include "Lifecycle/PostHogApplicationLifecycleHandler.h"
#include "PostHogDeveloperSettings.h"
#include "Templates/Function.h"

class IPostHogStorageProvider;
class IPostHogBatchTransport;
class FPostHogEventQueue;
class FPostHogApplicationLifecycleHandler;
class FPostHogIdentityManager;
class FPostHogSessionManager;
class FPostHogSuperPropertiesManager;
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
	// $process_person_profile is computed internally from the PersonProfilesPolicy snapshot
	// captured at EnableCollection time and IdentityManager->IsIdentified(); it can never be
	// influenced by CallProperties or super properties (both already strip reserved keys via
	// PostHogCapturePolicy::GetReservedPropertyKeys()).
	EPostHogCaptureResult CaptureEvent(const FString& EventName, UPostHogEventProperties* CallProperties);

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

	// Blank (or whitespace-only) GroupType/GroupKey is a safe no-op: no state mutation, no
	// event. Otherwise persists membership via IdentityManager first (so this same event's
	// $groups reflects it via ApplyGroups), then emits $groupidentify with $group_type,
	// $group_key, and $group_set (only when GroupProperties is non-null and non-empty).
	// GroupProperties is deep-copied into the captured event via UPostHogEventProperties::AddObject.
	EPostHogCaptureResult Group(const FString& GroupType, const FString& GroupKey, UPostHogEventProperties* GroupProperties);

	// Clears all persisted group membership. No event emitted. Safe no-op when not opted in.
	void ResetGroups();

	// Blank/whitespace-only Exception.Message or Exception.Type is a safe no-op (returns
	// InvalidEventName; no event, no property objects retained). Otherwise builds SDK-owned
	// exception properties ($exception_list and $exception_* summary fields) layered over
	// caller-supplied Properties (SDK-owned keys always win on collision) and emits via
	// CaptureEvent(TEXT("$exception"), ...), so consent, session, before-send, persistence, and
	// retry behavior are inherited rather than reimplemented.
	EPostHogCaptureResult CaptureException(const FPostHogExceptionInput& Exception, UPostHogEventProperties* Properties);

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

	// Sets $groups to the current persisted group membership, run last in CaptureEvent's
	// precedence order so it reflects any Group() call made earlier in the same call chain.
	// Writes nothing when there is no membership, matching Unity's "only add when non-empty".
	void ApplyGroups(FPostHogEvent& Event) const;

	FStorageProviderFactory StorageProviderFactory;
	FTransportFactory TransportFactory;
	FUuidGenerator UuidGenerator;
	FLifecycleMetadataProvider LifecycleMetadataProvider;
	FPostHogBeforeSendDelegate BeforeSend;

	bool bIsOptedIn = false;

	EPostHogPersonProfiles PersonProfilesPolicy = EPostHogPersonProfiles::IdentifiedOnly;

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
