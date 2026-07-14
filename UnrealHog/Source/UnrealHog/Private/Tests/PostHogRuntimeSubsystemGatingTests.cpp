// Trevor Eckhoff, 2026. All rights reserved.

#include "Subsystems/PostHogRuntimeSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "PostHogDeveloperSettings.h"
#include "Tests/PostHogTestPropertyHelpers.h"
#include "UObject/UnrealType.h"

namespace
{
	// Guarantees the process-wide settings CDO is restored to its original values when the
	// test function returns, whether that is a normal return or an early one from a failed check.
	struct FScopedDeveloperSettingsCdoRestore
	{
		FScopedDeveloperSettingsCdoRestore()
			: Cdo(GetMutableDefault<UPostHogDeveloperSettings>())
			, bOriginalAnalyticsEnabled(Cdo->IsAnalyticsEnabled())
			, OriginalApiKey(Cdo->GetApiKey())
		{
		}

		~FScopedDeveloperSettingsCdoRestore()
		{
			UnrealHogTests::SetPropertyValue<bool>(Cdo, TEXT("bAnalyticsEnabled"), bOriginalAnalyticsEnabled);
			UnrealHogTests::SetPropertyValue<FString>(Cdo, TEXT("ApiKey"), OriginalApiKey);
		}

		UPostHogDeveloperSettings* Cdo;
		bool bOriginalAnalyticsEnabled;
		FString OriginalApiKey;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogRuntimeSubsystemGatingTest, "UnrealHog.Subsystems.RuntimeSubsystem.ShouldCreateSubsystemGating", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogRuntimeSubsystemGatingTest::RunTest(const FString& Parameters)
{
	FScopedDeveloperSettingsCdoRestore RestoreGuard;

	// Case 1: analytics disabled entirely.
	UnrealHogTests::SetPropertyValue<bool>(RestoreGuard.Cdo, TEXT("bAnalyticsEnabled"), false);
	UnrealHogTests::SetPropertyValue<FString>(RestoreGuard.Cdo, TEXT("ApiKey"), TEXT("phc_valid_key"));
	{
		UPostHogRuntimeSubsystem* Subsystem = NewObject<UPostHogRuntimeSubsystem>(GetTransientPackage());
		TestFalse(TEXT("Disabled analytics prevents subsystem creation"), Subsystem->ShouldCreateSubsystem(GetTransientPackage()));
	}

	// Case 2: analytics enabled, but whitespace-only API key.
	UnrealHogTests::SetPropertyValue<bool>(RestoreGuard.Cdo, TEXT("bAnalyticsEnabled"), true);
	UnrealHogTests::SetPropertyValue<FString>(RestoreGuard.Cdo, TEXT("ApiKey"), TEXT("   "));
	{
		UPostHogRuntimeSubsystem* Subsystem = NewObject<UPostHogRuntimeSubsystem>(GetTransientPackage());
		TestFalse(TEXT("Whitespace API key prevents subsystem creation"), Subsystem->ShouldCreateSubsystem(GetTransientPackage()));
	}

	// Case 3: analytics enabled with a valid key.
	UnrealHogTests::SetPropertyValue<bool>(RestoreGuard.Cdo, TEXT("bAnalyticsEnabled"), true);
	UnrealHogTests::SetPropertyValue<FString>(RestoreGuard.Cdo, TEXT("ApiKey"), TEXT("phc_valid_key"));
	{
		UPostHogRuntimeSubsystem* Subsystem = NewObject<UPostHogRuntimeSubsystem>(GetTransientPackage());
		TestTrue(TEXT("Valid enabled configuration allows subsystem creation"), Subsystem->ShouldCreateSubsystem(GetTransientPackage()));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogRuntimeSubsystemUninitializedCaptureIsSafeNoOpTest, "UnrealHog.Subsystems.RuntimeSubsystem.CaptureEventBeforeInitializeIsSafeNoOp", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogRuntimeSubsystemUninitializedCaptureIsSafeNoOpTest::RunTest(const FString& Parameters)
{
	// Constructed without Initialize() ever running, so EventQueue is null; CaptureEvent must
	// be a safe no-op rather than dereferencing a null queue.
	UPostHogRuntimeSubsystem* Subsystem = NewObject<UPostHogRuntimeSubsystem>(GetTransientPackage());
	Subsystem->CaptureEvent(TEXT("uninitialized_capture"));

	// Reaching this point without a crash is the assertion.
	TestTrue(TEXT("CaptureEvent returned safely with no EventQueue"), true);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
