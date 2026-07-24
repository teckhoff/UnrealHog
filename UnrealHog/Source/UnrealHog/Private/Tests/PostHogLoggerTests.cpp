#include "Logging/PostHogLogger.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

namespace
{
	// RAII guard that snapshots and restores the process-global LogUnrealHog verbosity so a test that
	// calls SetVerbosity cannot contaminate any other test that logs to the category.
	struct FScopedLogUnrealHogVerbosity
	{
		FScopedLogUnrealHogVerbosity() : Saved(LogUnrealHog.GetVerbosity()) {}
		~FScopedLogUnrealHogVerbosity() { LogUnrealHog.SetVerbosity(Saved); }
		ELogVerbosity::Type Saved;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogLoggerMapsLevelsToVerbosityTest, "UnrealHog.Logging.Logger.MapsLevelsToVerbosity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogLoggerMapsLevelsToVerbosityTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Debug maps to VeryVerbose"), static_cast<int32>(PostHogLogger::ToVerbosity(EPostHogLogLevel::Debug)), static_cast<int32>(ELogVerbosity::VeryVerbose));
	TestEqual(TEXT("Info maps to Log"), static_cast<int32>(PostHogLogger::ToVerbosity(EPostHogLogLevel::Info)), static_cast<int32>(ELogVerbosity::Log));
	TestEqual(TEXT("Warning maps to Warning"), static_cast<int32>(PostHogLogger::ToVerbosity(EPostHogLogLevel::Warning)), static_cast<int32>(ELogVerbosity::Warning));
	TestEqual(TEXT("Error maps to Error"), static_cast<int32>(PostHogLogger::ToVerbosity(EPostHogLogLevel::Error)), static_cast<int32>(ELogVerbosity::Error));
	TestEqual(TEXT("None maps to NoLogging"), static_cast<int32>(PostHogLogger::ToVerbosity(EPostHogLogLevel::None)), static_cast<int32>(ELogVerbosity::NoLogging));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogLoggerCategoryThresholdFiltersBySeverityTest, "UnrealHog.Logging.Logger.CategoryThresholdFiltersBySeverity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogLoggerCategoryThresholdFiltersBySeverityTest::RunTest(const FString& Parameters)
{
	// SetVerbosity mutates process-global category state; restore it on scope exit (including on an
	// assertion failure) so this test cannot leave LogUnrealHog at, e.g., NoLogging for other tests.
	FScopedLogUnrealHogVerbosity VerbosityGuard;

	struct FLevelExpectation
	{
		EPostHogLogLevel Level;
		const TCHAR* Name;
		bool bError;
		bool bWarning;
		bool bLog;
		bool bVerbose;
		bool bVeryVerbose;
	};

	// Each row is the exact admitted set for the configured level: the default Warning admits only
	// Error and Warning; None admits nothing; Debug admits every UnrealHog verbosity; Info admits Log
	// and above; Error admits only Error.
	const FLevelExpectation Expectations[] =
	{
		{ EPostHogLogLevel::Warning, TEXT("Warning"), true,  true,  false, false, false },
		{ EPostHogLogLevel::Debug,   TEXT("Debug"),   true,  true,  true,  true,  true  },
		{ EPostHogLogLevel::Info,    TEXT("Info"),    true,  true,  true,  false, false },
		{ EPostHogLogLevel::Error,   TEXT("Error"),   true,  false, false, false, false },
		{ EPostHogLogLevel::None,    TEXT("None"),    false, false, false, false, false },
	};

	for (const FLevelExpectation& Expectation : Expectations)
	{
		PostHogLogger::ApplyConfiguredLevel(Expectation.Level);

		// The category threshold is the authoritative test seam the UE_LOG macros consult before doing
		// any formatting work; !IsSuppressed(V) is exactly "verbosity V is admitted at this level".
		const bool bAdmitsError = !LogUnrealHog.IsSuppressed(ELogVerbosity::Error);
		const bool bAdmitsWarning = !LogUnrealHog.IsSuppressed(ELogVerbosity::Warning);
		const bool bAdmitsLog = !LogUnrealHog.IsSuppressed(ELogVerbosity::Log);
		const bool bAdmitsVerbose = !LogUnrealHog.IsSuppressed(ELogVerbosity::Verbose);
		const bool bAdmitsVeryVerbose = !LogUnrealHog.IsSuppressed(ELogVerbosity::VeryVerbose);

		TestEqual(FString::Printf(TEXT("%s admits Error"), Expectation.Name), bAdmitsError, Expectation.bError);
		TestEqual(FString::Printf(TEXT("%s admits Warning"), Expectation.Name), bAdmitsWarning, Expectation.bWarning);
		TestEqual(FString::Printf(TEXT("%s admits Log"), Expectation.Name), bAdmitsLog, Expectation.bLog);
		TestEqual(FString::Printf(TEXT("%s admits Verbose"), Expectation.Name), bAdmitsVerbose, Expectation.bVerbose);
		TestEqual(FString::Printf(TEXT("%s admits VeryVerbose"), Expectation.Name), bAdmitsVeryVerbose, Expectation.bVeryVerbose);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
