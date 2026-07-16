#pragma once

#include "CoreMinimal.h"
#include "Events/PostHogBeforeSend.h"
#include "Templates/Function.h"

class IPostHogStorageProvider;
class IPostHogBatchTransport;
class FPostHogEventQueue;
class UPostHogDeveloperSettings;
class UPostHogEventProperties;
struct FPostHogEvent;

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

	FPostHogConsentController(FStorageProviderFactory InStorageProviderFactory,
		FTransportFactory InTransportFactory, FUuidGenerator InUuidGenerator);
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

	void Flush();

	const FString& GetSessionId() const { return SessionId; }
	FPostHogEventQueue* GetEventQueue() const { return EventQueue.Get(); }
	int32 GetQueuedEventCount() const;

	int32 GetStorageProviderCreationCount() const { return StorageProviderCreationCount; }
	int32 GetTransportCreationCount() const { return TransportCreationCount; }
	int32 GetSessionCreationCount() const { return SessionCreationCount; }

private:
	bool EnableCollection(const UPostHogDeveloperSettings& Settings, FString& OutFailureReason);
	void DisableCollection();

	void PersistOptIn(bool bOptIn) const;
	static bool TryLoadPersistedOptIn(IPostHogStorageProvider& StorageProvider, bool& OutOptedIn);

	// Extension points for future EPs. No-ops today: EP-004 does not implement super-property,
	// session, or group storage, but CaptureEvent's precedence order already reserves their slot.
	void ApplySuperProperties(FPostHogEvent& Event) const;
	void ApplyGroups(FPostHogEvent& Event) const;

	FStorageProviderFactory StorageProviderFactory;
	FTransportFactory TransportFactory;
	FUuidGenerator UuidGenerator;
	FPostHogBeforeSendDelegate BeforeSend;

	bool bIsOptedIn = false;
	FString SessionId;

	TUniquePtr<IPostHogStorageProvider> StorageProvider;
	TUniquePtr<IPostHogBatchTransport> Transport;
	TUniquePtr<FPostHogEventQueue> EventQueue;

	int32 StorageProviderCreationCount = 0;
	int32 TransportCreationCount = 0;
	int32 SessionCreationCount = 0;
};
