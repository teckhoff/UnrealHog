// Trevor Eckhoff, 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

class IPostHogStorageProvider;
class IPostHogBatchTransport;
class FPostHogEventQueue;
class UPostHogDeveloperSettings;
struct FPostHogEvent;

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

	bool Capture(const FPostHogEvent& Event);
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

	FStorageProviderFactory StorageProviderFactory;
	FTransportFactory TransportFactory;
	FUuidGenerator UuidGenerator;

	bool bIsOptedIn = false;
	FString SessionId;

	TUniquePtr<IPostHogStorageProvider> StorageProvider;
	TUniquePtr<IPostHogBatchTransport> Transport;
	TUniquePtr<FPostHogEventQueue> EventQueue;

	int32 StorageProviderCreationCount = 0;
	int32 TransportCreationCount = 0;
	int32 SessionCreationCount = 0;
};
