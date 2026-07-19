#include "ErrorTracking/PostHogExceptionCapture.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Consent/PostHogConsentController.h"
#include "CoreGlobals.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Events/PostHogBatchPayload.h"
#include "PostHogDeveloperSettings.h"
#include "Storage/PostHogFileStorageProvider.h"
#include "Tests/PostHogFakeBatchTransport.h"
#include "Tests/PostHogTestPropertyHelpers.h"
#include "UObject/Package.h"

namespace
{
	// RAII fixture that owns a unique temporary directory for the file storage provider backing
	// these exception capture tests; removed on scope exit even if an assertion fails mid-test.
	class FScopedExceptionCaptureTestStorageDirectory
	{
	public:
		FScopedExceptionCaptureTestStorageDirectory()
			: RootPath(FPaths::Combine(FPlatformProcess::UserTempDir(), TEXT("UnrealHogExceptionCaptureTests"), FGuid::NewGuid().ToString(EGuidFormats::Digits)))
		{
		}

		~FScopedExceptionCaptureTestStorageDirectory()
		{
			IFileManager::Get().DeleteDirectory(*RootPath, false, true);
		}

		const FString& GetRootPath() const { return RootPath; }

	private:
		FString RootPath;
	};

	UPostHogDeveloperSettings* MakeExceptionCaptureTestSettings()
	{
		UPostHogDeveloperSettings* Settings = NewObject<UPostHogDeveloperSettings>(GetTransientPackage());
		UnrealHogTests::SetPropertyValue<FString>(Settings, TEXT("ApiKey"), TEXT("phc_valid_key"));
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bAnalyticsEnabled"), true);
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bDefaultUserOptIn"), false);
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bCaptureApplicationLifecycleEvents"), false);
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bPreloadFeatureFlags"), false);
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bSessionReplay"), false);
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bReuseAnonymousId"), false);
		return Settings;
	}

	FPostHogConsentController::FStorageProviderFactory MakeExceptionCaptureStorageFactory(const FString& RootPath)
	{
		return [RootPath]() -> TUniquePtr<IPostHogStorageProvider>
		{
			return MakeUnique<FPostHogFileStorageProvider>(RootPath);
		};
	}

	// Captures the most recently created fake transport so tests can drive its completion callbacks.
	FPostHogConsentController::FTransportFactory MakeExceptionCaptureTransportFactory(FPostHogFakeBatchTransport*& OutLastTransport)
	{
		return [&OutLastTransport](const FString&) -> TUniquePtr<IPostHogBatchTransport>
		{
			TUniquePtr<FPostHogFakeBatchTransport> Transport = MakeUnique<FPostHogFakeBatchTransport>();
			OutLastTransport = Transport.Get();
			return Transport;
		};
	}

	// Deterministic, countable stand-in for PostHogUuidV7::New().
	FPostHogConsentController::FUuidGenerator MakeExceptionCaptureUuidGenerator(int32& Counter)
	{
		return [&Counter]() { return FString::Printf(TEXT("exception-capture-uuid-%d"), ++Counter); };
	}

	int32 CountQueuedExceptionEvents(FPostHogConsentController& Controller, FPostHogFakeBatchTransport*& LastTransport)
	{
		Controller.Flush();
		if (!LastTransport)
		{
			return 0;
		}

		const TSharedRef<FJsonObject> PayloadJson = LastTransport->GetLastPayload().ToJsonObject();
		const TArray<TSharedPtr<FJsonValue>>* BatchArray = nullptr;
		int32 Count = 0;
		if (PayloadJson->TryGetArrayField(TEXT("batch"), BatchArray))
		{
			Count = BatchArray->Num();
		}

		LastTransport->CompleteLast(true, 200, TEXT(""));
		return Count;
	}

	// Flushes and returns the last (and expected-only) queued event's JSON object, or nullptr when
	// no batch was sent. Completes the transport so the queue is left in a clean state.
	TSharedPtr<FJsonObject> GetLastQueuedExceptionEvent(FPostHogConsentController& Controller, FPostHogFakeBatchTransport*& LastTransport)
	{
		Controller.Flush();
		if (!LastTransport)
		{
			return nullptr;
		}

		const TSharedRef<FJsonObject> PayloadJson = LastTransport->GetLastPayload().ToJsonObject();
		const TArray<TSharedPtr<FJsonValue>>* BatchArray = nullptr;
		TSharedPtr<FJsonObject> LastEvent;
		if (PayloadJson->TryGetArrayField(TEXT("batch"), BatchArray) && BatchArray->Num() > 0)
		{
			LastEvent = (*BatchArray)[BatchArray->Num() - 1]->AsObject();
		}

		LastTransport->CompleteLast(true, 200, TEXT(""));
		return LastEvent;
	}

	// Fixture bundling a real opted-in FPostHogConsentController with a fake transport, matching
	// the pattern used by PostHogConsentControllerExceptionTests.cpp.
	struct FExceptionCaptureFixture
	{
		FScopedExceptionCaptureTestStorageDirectory StorageDirectory;
		FPostHogFakeBatchTransport* LastTransport = nullptr;
		int32 UuidCounter = 0;
		UPostHogDeveloperSettings* Settings = nullptr;
		TUniquePtr<FPostHogConsentController> Controller;

		FExceptionCaptureFixture()
		{
			Settings = MakeExceptionCaptureTestSettings();
			Controller = MakeUnique<FPostHogConsentController>(
				MakeExceptionCaptureStorageFactory(StorageDirectory.GetRootPath()),
				MakeExceptionCaptureTransportFactory(LastTransport),
				MakeExceptionCaptureUuidGenerator(UuidCounter));
			Controller->Initialize(*Settings);
			Controller->SetOptIn(true, *Settings);
		}

		int32 CountQueuedEvents()
		{
			return CountQueuedExceptionEvents(*Controller, LastTransport);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogExceptionCaptureEnabledEmitsOneTest, "UnrealHog.ErrorTracking.ExceptionCapture.EnabledEmitsOne", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogExceptionCaptureEnabledEmitsOneTest::RunTest(const FString& Parameters)
{
	FExceptionCaptureFixture Fixture;
	FPostHogExceptionCapture Capture(*Fixture.Controller);

	Capture.RegisterHandlers(true, 0);
	TestTrue(TEXT("Registered when enabled (allowed in editor)"), Capture.IsRegistered());

	Capture.SimulateEnsureFailed("false", "SomeFile.cpp", 42, TEXT("A message"), TEXT("Ensure failed: false"));

	TestEqual(TEXT("Exactly one $exception queued"), Fixture.CountQueuedEvents(), 1);

	Capture.UnregisterHandlers();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogExceptionCaptureDisabledEmitsNoneTest, "UnrealHog.ErrorTracking.ExceptionCapture.DisabledEmitsNone", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogExceptionCaptureDisabledEmitsNoneTest::RunTest(const FString& Parameters)
{
	FExceptionCaptureFixture Fixture;
	FPostHogExceptionCapture Capture(*Fixture.Controller);

	// bAllowInEditor=false: Automation tests run under EditorContext, so GIsEditor is true here,
	// exercising the editor-exclusion path without needing a non-editor test target. Since the
	// gate lives in RegisterHandlers (the real FCoreDelegates::OnEnsureFailed is never bound),
	// no SimulateEnsureFailed call is made here: that seam re-enacts what a bound delegate would
	// have received, and an unbound delegate never fires in the first place.
	Capture.RegisterHandlers(false, 0);
	TestFalse(TEXT("Not registered when editor-excluded"), Capture.IsRegistered());

	TestEqual(TEXT("No $exception queued when not registered"), Fixture.Controller->GetQueuedEventCount(), 0);
	if (TestNotNull(TEXT("Opt-in eagerly creates the transport, independent of capture"), Fixture.LastTransport))
	{
		TestEqual(TEXT("No batch sent"), Fixture.LastTransport->GetSentCount(), 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogExceptionCaptureNoDuplicateRegistrationTest, "UnrealHog.ErrorTracking.ExceptionCapture.NoDuplicateRegistration", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogExceptionCaptureNoDuplicateRegistrationTest::RunTest(const FString& Parameters)
{
	FExceptionCaptureFixture Fixture;
	FPostHogExceptionCapture Capture(*Fixture.Controller);

	Capture.RegisterHandlers(true, 0);
	Capture.RegisterHandlers(true, 0);
	TestTrue(TEXT("Registered after duplicate RegisterHandlers calls"), Capture.IsRegistered());

	Capture.SimulateEnsureFailed("false", "SomeFile.cpp", 42, TEXT("A message"), TEXT("Ensure failed: false"));

	TestEqual(TEXT("Exactly one capture despite duplicate registration"), Fixture.CountQueuedEvents(), 1);

	Capture.UnregisterHandlers();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogExceptionCaptureDebounceBoundaryTest, "UnrealHog.ErrorTracking.ExceptionCapture.DebounceBoundary", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogExceptionCaptureDebounceBoundaryTest::RunTest(const FString& Parameters)
{
	FExceptionCaptureFixture Fixture;

	double FakeNow = 0.0;
	FPostHogExceptionCapture Capture(*Fixture.Controller, [&FakeNow]() { return FakeNow; });

	Capture.RegisterHandlers(true, 1000);

	FakeNow = 0.0;
	Capture.SimulateEnsureFailed("false", "SomeFile.cpp", 1, TEXT("First"), TEXT("Ensure failed: first"));

	FakeNow = 0.5;
	Capture.SimulateEnsureFailed("false", "SomeFile.cpp", 2, TEXT("Second"), TEXT("Ensure failed: second"));

	FakeNow = 1.0;
	Capture.SimulateEnsureFailed("false", "SomeFile.cpp", 3, TEXT("Third"), TEXT("Ensure failed: third"));

	TestEqual(TEXT("Exactly 2 captures across 3 calls (second debounced)"), Fixture.CountQueuedEvents(), 2);

	Capture.UnregisterHandlers();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogExceptionCaptureHandlerRemovalProvenTest, "UnrealHog.ErrorTracking.ExceptionCapture.HandlerRemovalProven", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogExceptionCaptureHandlerRemovalProvenTest::RunTest(const FString& Parameters)
{
	FExceptionCaptureFixture Fixture;
	FPostHogExceptionCapture Capture(*Fixture.Controller);

	TestFalse(TEXT("Not registered before RegisterHandlers"), Capture.IsRegistered());

	Capture.RegisterHandlers(true, 0);
	TestTrue(TEXT("Registered after RegisterHandlers"), Capture.IsRegistered());

	Capture.UnregisterHandlers();
	TestFalse(TEXT("Not registered after UnregisterHandlers"), Capture.IsRegistered());

	// Idempotent: calling again must not crash or flip state.
	Capture.UnregisterHandlers();
	TestFalse(TEXT("Still not registered after repeated UnregisterHandlers"), Capture.IsRegistered());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogExceptionCaptureAttachesPersonUrlOnceTest, "UnrealHog.ErrorTracking.ExceptionCapture.AttachesPersonUrlOnce", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogExceptionCaptureAttachesPersonUrlOnceTest::RunTest(const FString& Parameters)
{
	FExceptionCaptureFixture Fixture;
	Fixture.Controller->Identify(TEXT("user-1"), nullptr, nullptr);
	GetLastQueuedExceptionEvent(*Fixture.Controller, Fixture.LastTransport);

	FPostHogExceptionCapture Capture(*Fixture.Controller);
	Capture.RegisterHandlers(true, 0);

	Capture.SimulateEnsureFailed("false", "SomeFile.cpp", 42, TEXT("A message"), TEXT("Ensure failed: false"));

	const TSharedPtr<FJsonObject> Event = GetLastQueuedExceptionEvent(*Fixture.Controller, Fixture.LastTransport);
	if (!TestTrue(TEXT("Exception event queued"), Event.IsValid()))
	{
		Capture.UnregisterHandlers();
		return false;
	}

	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("Event has properties"), Event->TryGetObjectField(TEXT("properties"), PropertiesObject));

	FString PersonUrl;
	TestTrue(TEXT("properties has $exception_personURL exactly once"), (*PropertiesObject)->TryGetStringField(TEXT("$exception_personURL"), PersonUrl));
	TestEqual(TEXT("Person URL matches identified user"), PersonUrl, FString(TEXT("https://us.posthog.com/project/phc_valid_key/person/user-1")));

	Capture.UnregisterHandlers();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
