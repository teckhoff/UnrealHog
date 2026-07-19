#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Consent/PostHogConsentController.h"
#include "Events/PostHogEventQueue.h"
#include "PostHogDeveloperSettings.h"
#include "Storage/PostHogStorageProvider.h"
#include "Tests/PostHogFakeBatchTransport.h"
#include "Tests/PostHogFakeClock.h"
#include "Tests/PostHogFakeReachabilityProvider.h"
#include "Tests/PostHogInMemoryStorageProvider.h"
#include "Tests/PostHogTestPropertyHelpers.h"
#include "UObject/Package.h"

namespace PostHogAcceptance
{
	// Non-owning IPostHogStorageProvider that forwards every call to a fixture-owned storage
	// instance. FPostHogConsentController takes ownership of whatever its storage factory
	// returns and destroys it on opt-out/Shutdown()/destruction; wrapping the acceptance
	// fixture's single FPostHogInMemoryStorageProvider in this adapter lets a standalone
	// FPostHogEventQueue reopen that same in-memory data after the controller that composed it
	// is gone, mirroring how a restarted FPostHogFileStorageProvider instance reopens the same
	// on-disk directory in PostHogEventQueueTests.cpp's SimulatedRestart test.
	class FNonOwningStorageProviderAdapter final : public IPostHogStorageProvider
	{
	public:
		explicit FNonOwningStorageProviderAdapter(IPostHogStorageProvider& InTarget) : Target(InTarget) {}

		virtual bool SaveEvent(const FString& EventId, const FString& EventJson) override { return Target.SaveEvent(EventId, EventJson); }
		using IPostHogStorageProvider::SaveEvent;
		virtual bool LoadEvent(const FString& EventId, FString& EventJson) override { return Target.LoadEvent(EventId, EventJson); }
		virtual bool DeleteEvent(const FString& EventId) override { return Target.DeleteEvent(EventId); }
		virtual bool ClearEvents() override { return Target.ClearEvents(); }
		virtual TArray<FString> GetEventIds() override { return Target.GetEventIds(); }
		virtual int32 GetEventCount() override { return Target.GetEventCount(); }
		virtual bool SaveState(const FString& StateKey, const FString& StateJson) override { return Target.SaveState(StateKey, StateJson); }
		using IPostHogStorageProvider::SaveState;
		virtual bool LoadState(const FString& StateKey, FString& StateJson) override { return Target.LoadState(StateKey, StateJson); }
		virtual bool DeleteState(const FString& StateKey) override { return Target.DeleteState(StateKey); }
		virtual void FlushPendingWrites() override { Target.FlushPendingWrites(); }

	private:
		IPostHogStorageProvider& Target;
	};
}

/**
 * @brief Shared fixture for the EP-029 isolated core-ingress acceptance suite.
 *
 * Owns one in-memory storage instance, a slot for the most recently created fake transport, a
 * deterministic UUID counter, a fixed lifecycle metadata provider, and an owned fake clock and
 * reachability provider, so a FPostHogConsentController and standalone FPostHogEventQueue
 * instances can be exercised against the exact same durable state with zero disk or network I/O.
 */
struct FPostHogAcceptanceFixture
{
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;
	FPostHogInMemoryStorageProvider SharedStorage;
	FPostHogFakeClock Clock;
	FPostHogFakeReachabilityProvider Reachability{EPostHogReachabilityState::Reachable};

	FPostHogConsentController::FStorageProviderFactory MakeStorageFactory()
	{
		return [this]() -> TUniquePtr<IPostHogStorageProvider>
		{
			return MakeUnique<PostHogAcceptance::FNonOwningStorageProviderAdapter>(SharedStorage);
		};
	}

	FPostHogConsentController::FTransportFactory MakeTransportFactory()
	{
		return [this](const FString&) -> TUniquePtr<IPostHogBatchTransport>
		{
			TUniquePtr<FPostHogFakeBatchTransport> Transport = MakeUnique<FPostHogFakeBatchTransport>();
			LastTransport = Transport.Get();
			return Transport;
		};
	}

	FPostHogConsentController::FUuidGenerator MakeUuidGenerator()
	{
		return [this]() { return FString::Printf(TEXT("acceptance-uuid-%d"), ++UuidCounter); };
	}

	static FPostHogConsentController::FLifecycleMetadataProvider MakeLifecycleMetadataProvider()
	{
		return []()
		{
			FPostHogApplicationMetadata Metadata;
			Metadata.Version = TEXT("1.0.0");
			Metadata.Build = TEXT("acceptance-build-1");
			return Metadata;
		};
	}

	TUniquePtr<FPostHogConsentController> MakeController()
	{
		return MakeUnique<FPostHogConsentController>(MakeStorageFactory(), MakeTransportFactory(), MakeUuidGenerator(), MakeLifecycleMetadataProvider());
	}

	static UPostHogDeveloperSettings* MakeSettings(bool bValidApiKey,
		bool bAnalyticsEnabled,
		bool bDefaultUserOptIn,
		bool bCaptureApplicationLifecycleEvents = false,
		EPostHogPersonProfiles PersonProfiles = EPostHogPersonProfiles::IdentifiedOnly,
		int32 MaxQueueSize = 1000,
		int32 MaxBatchSize = 50,
		int32 FlushEventCount = 100,
		float FlushOnQuitTimeoutSeconds = 3.0f)
	{
		UPostHogDeveloperSettings* Settings = NewObject<UPostHogDeveloperSettings>(GetTransientPackage());
		UnrealHogTests::SetPropertyValue<FString>(Settings, TEXT("ApiKey"), bValidApiKey ? TEXT("phc_valid_key") : TEXT(""));
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bAnalyticsEnabled"), bAnalyticsEnabled);
			UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bDefaultUserOptIn"), bDefaultUserOptIn);
			UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bCaptureApplicationLifecycleEvents"), bCaptureApplicationLifecycleEvents);
			UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bPreloadFeatureFlags"), false);
			UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bSessionReplay"), false);
			UnrealHogTests::SetPropertyValue<EPostHogPersonProfiles>(Settings, TEXT("PersonProfiles"), PersonProfiles);
			UnrealHogTests::SetPropertyValue<int32>(Settings, TEXT("MaxQueueSize"), MaxQueueSize);
		UnrealHogTests::SetPropertyValue<int32>(Settings, TEXT("MaxBatchSize"), MaxBatchSize);
		UnrealHogTests::SetPropertyValue<int32>(Settings, TEXT("FlushEventCount"), FlushEventCount);
		UnrealHogTests::SetPropertyValue<float>(Settings, TEXT("FlushOnQuitTimeoutSeconds"), FlushOnQuitTimeoutSeconds);
		return Settings;
	}

	// Constructs a standalone queue against a caller-supplied storage/transport pair (typically
	// this fixture's SharedStorage plus either LastTransport or a fresh fake transport), wired to
	// this fixture's fake Clock and Reachability so retry/backoff/adaptive-413/offline behavior is
	// deterministic and never touches the network.
	TUniquePtr<FPostHogEventQueue> MakeStandaloneQueue(IPostHogStorageProvider& Storage,
		IPostHogBatchTransport& Transport,
		const FString& ApiKey,
		int32 MaxQueueSize,
		int32 MaxBatchSize,
		int32 FlushEventCount)
	{
		return MakeUnique<FPostHogEventQueue>(Storage, Transport, ApiKey, MaxQueueSize, MaxBatchSize, FlushEventCount, &Clock, &Reachability);
	}
};

#endif // WITH_DEV_AUTOMATION_TESTS
