#include "Lifecycle/PostHogQuitFlushCoordinator.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

namespace
{
	// Deterministic stand-in for the production FTSTicker-backed scheduler: captures the
	// timeout callback so tests can fire it directly instead of waiting on a real timer, and
	// counts how many times the returned cancel closure runs.
	struct FFakeQuitFlushFixture
	{
		int32 RequestFlushCallCount = 0;
		int32 ScheduleTimeoutCallCount = 0;
		int32 CancelTimeoutCallCount = 0;
		int32 ShutdownCallCount = 0;
		int32 RequestExitCallCount = 0;

		FPostHogEventQueueFlushComplete LastFlushCompletion;
		TFunction<void()> LastTimeoutCallback;

		FPostHogQuitFlushCoordinator::FRequestFlushFunc MakeRequestFlushFunc()
		{
			return [this](FPostHogEventQueueFlushComplete OnComplete)
			{
				++RequestFlushCallCount;
				LastFlushCompletion = MoveTemp(OnComplete);
			};
		}

		FPostHogQuitFlushCoordinator::FShutdownFunc MakeShutdownFunc()
		{
			return [this]() { ++ShutdownCallCount; };
		}

		FPostHogQuitFlushCoordinator::FRequestExitFunc MakeRequestExitFunc()
		{
			return [this]() { ++RequestExitCallCount; };
		}

		FPostHogQuitFlushCoordinator::FScheduleTimeoutFunc MakeScheduleTimeoutFunc()
		{
			return [this](float, TFunction<void()> OnTimeout) -> TFunction<void()>
			{
				++ScheduleTimeoutCallCount;
				LastTimeoutCallback = MoveTemp(OnTimeout);

				return [this]() { ++CancelTimeoutCallCount; };
			};
		}

		TUniquePtr<FPostHogQuitFlushCoordinator> MakeCoordinator(float TimeoutSeconds = 3.0f)
		{
			return MakeUnique<FPostHogQuitFlushCoordinator>(
				MakeRequestFlushFunc(),
				MakeShutdownFunc(),
				TimeoutSeconds,
				MakeRequestExitFunc(),
				MakeScheduleTimeoutFunc());
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogQuitFlushCoordinatorSuccessBeforeTimeoutTest, "UnrealHog.Lifecycle.QuitFlushCoordinator.SuccessBeforeTimeoutFinalizesOnceAndCancelsTimeout", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogQuitFlushCoordinatorSuccessBeforeTimeoutTest::RunTest(const FString& Parameters)
{
	FFakeQuitFlushFixture Fixture;
	TUniquePtr<FPostHogQuitFlushCoordinator> Coordinator = Fixture.MakeCoordinator();

	Coordinator->BeginFlushAndQuit();

	TestEqual(TEXT("RequestFlush called once"), Fixture.RequestFlushCallCount, 1);
	TestEqual(TEXT("Timeout scheduled once"), Fixture.ScheduleTimeoutCallCount, 1);
	TestFalse(TEXT("Not finalized before flush completes"), Coordinator->IsFinalized());

	if (!TestTrue(TEXT("Flush completion captured"), (bool)Fixture.LastFlushCompletion))
	{
		return false;
	}

	Fixture.LastFlushCompletion(EPostHogEventQueueFlushResult::Drained);

	TestTrue(TEXT("Finalized after flush completion"), Coordinator->IsFinalized());
	TestEqual(TEXT("Shutdown called exactly once"), Fixture.ShutdownCallCount, 1);
	TestEqual(TEXT("RequestExit called exactly once"), Fixture.RequestExitCallCount, 1);
	TestEqual(TEXT("Timeout cancelled exactly once"), Fixture.CancelTimeoutCallCount, 1);

	// A late-arriving timeout after finalization must not run finalization again.
	if (Fixture.LastTimeoutCallback)
	{
		Fixture.LastTimeoutCallback();
	}

	TestEqual(TEXT("Shutdown still called exactly once after late timeout"), Fixture.ShutdownCallCount, 1);
	TestEqual(TEXT("RequestExit still called exactly once after late timeout"), Fixture.RequestExitCallCount, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogQuitFlushCoordinatorTimeoutFirstTest, "UnrealHog.Lifecycle.QuitFlushCoordinator.TimeoutFirstFinalizesOnceAndIgnoresLateFlush", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogQuitFlushCoordinatorTimeoutFirstTest::RunTest(const FString& Parameters)
{
	FFakeQuitFlushFixture Fixture;
	TUniquePtr<FPostHogQuitFlushCoordinator> Coordinator = Fixture.MakeCoordinator();

	Coordinator->BeginFlushAndQuit();

	if (!TestTrue(TEXT("Timeout callback captured"), (bool)Fixture.LastTimeoutCallback))
	{
		return false;
	}

	Fixture.LastTimeoutCallback();

	TestTrue(TEXT("Finalized after timeout"), Coordinator->IsFinalized());
	TestEqual(TEXT("Shutdown called exactly once"), Fixture.ShutdownCallCount, 1);
	TestEqual(TEXT("RequestExit called exactly once"), Fixture.RequestExitCallCount, 1);

	// A flush completion that arrives after the timeout already finalized must be a no-op.
	if (!TestTrue(TEXT("Flush completion captured"), (bool)Fixture.LastFlushCompletion))
	{
		return false;
	}

	Fixture.LastFlushCompletion(EPostHogEventQueueFlushResult::Drained);

	TestEqual(TEXT("Shutdown still called exactly once after late flush completion"), Fixture.ShutdownCallCount, 1);
	TestEqual(TEXT("RequestExit still called exactly once after late flush completion"), Fixture.RequestExitCallCount, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogQuitFlushCoordinatorOverlappingBeginCallsTest, "UnrealHog.Lifecycle.QuitFlushCoordinator.OverlappingBeginFlushAndQuitCallsFinalizeOnce", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogQuitFlushCoordinatorOverlappingBeginCallsTest::RunTest(const FString& Parameters)
{
	FFakeQuitFlushFixture Fixture;
	TUniquePtr<FPostHogQuitFlushCoordinator> Coordinator = Fixture.MakeCoordinator();

	// Simulates the window-close veto and an explicit FlushAndQuit() API call racing.
	Coordinator->BeginFlushAndQuit();
	Coordinator->BeginFlushAndQuit();

	TestEqual(TEXT("RequestFlush called exactly once despite two BeginFlushAndQuit calls"), Fixture.RequestFlushCallCount, 1);
	TestEqual(TEXT("Timeout scheduled exactly once despite two BeginFlushAndQuit calls"), Fixture.ScheduleTimeoutCallCount, 1);

	if (!TestTrue(TEXT("Flush completion captured"), (bool)Fixture.LastFlushCompletion))
	{
		return false;
	}

	Fixture.LastFlushCompletion(EPostHogEventQueueFlushResult::Drained);

	// A third call after finalization must also be a no-op.
	Coordinator->BeginFlushAndQuit();

	TestEqual(TEXT("RequestFlush still called exactly once"), Fixture.RequestFlushCallCount, 1);
	TestEqual(TEXT("RequestExit called exactly once"), Fixture.RequestExitCallCount, 1);
	TestEqual(TEXT("Shutdown called exactly once"), Fixture.ShutdownCallCount, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogQuitFlushCoordinatorDestroyedWhileFlushPendingTest, "UnrealHog.Lifecycle.QuitFlushCoordinator.DestroyedWhileFlushPendingIgnoresLateCallbacks", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogQuitFlushCoordinatorDestroyedWhileFlushPendingTest::RunTest(const FString& Parameters)
{
	// Mirrors UPostHogRuntimeSubsystem::Deinitialize destroying QuitCoordinator while a
	// BeginFlushAndQuit() it started is still in flight (e.g. EventQueue holds the flush
	// completion callback). The coordinator must not be touched by callbacks that arrive after
	// its destruction.
	FFakeQuitFlushFixture Fixture;
	TUniquePtr<FPostHogQuitFlushCoordinator> Coordinator = Fixture.MakeCoordinator();

	Coordinator->BeginFlushAndQuit();

	if (!TestTrue(TEXT("Flush completion captured"), (bool)Fixture.LastFlushCompletion) ||
		!TestTrue(TEXT("Timeout callback captured"), (bool)Fixture.LastTimeoutCallback))
	{
		return false;
	}

	FPostHogEventQueueFlushComplete DanglingFlushCompletion = Fixture.LastFlushCompletion;
	TFunction<void()> DanglingTimeoutCallback = Fixture.LastTimeoutCallback;

	Coordinator.Reset();

	// Both callbacks emulate EventQueue/the fake ticker invoking a callback that outlived the
	// coordinator; neither may dereference the destroyed object or run Shutdown/RequestExit.
	DanglingFlushCompletion(EPostHogEventQueueFlushResult::Cancelled);
	DanglingTimeoutCallback();

	TestEqual(TEXT("Shutdown never invoked for a coordinator destroyed before finalization"), Fixture.ShutdownCallCount, 0);
	TestEqual(TEXT("RequestExit never invoked for a coordinator destroyed before finalization"), Fixture.RequestExitCallCount, 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
