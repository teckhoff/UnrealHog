#include "Session/PostHogSessionManager.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/DateTime.h"
#include "Misc/Timespan.h"

namespace
{
	FPostHogSessionManager::FSessionIdGenerator MakeCountingIdGenerator(int32& Counter)
	{
		return [&Counter]() { return FString::Printf(TEXT("session-%d"), ++Counter); };
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogSessionManagerInactivityBoundaryTest, "UnrealHog.Session.SessionManager.RotatesOnlyStrictlyAfterInactivityBoundary", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogSessionManagerInactivityBoundaryTest::RunTest(const FString& Parameters)
{
	FDateTime Now(2026, 1, 1, 0, 0, 0);
	int32 Counter = 0;

	FPostHogSessionManager SessionManager([&Now]() { return Now; }, MakeCountingIdGenerator(Counter));
	SessionManager.SetCollectionPermitted(true);

	const FString FirstSessionId = SessionManager.GetSessionId();
	TestFalse(TEXT("First session id is non-empty"), FirstSessionId.IsEmpty());

	Now += FTimespan::FromMinutes(30.0);
	TestEqual(TEXT("Same session id at exactly the 30-minute boundary"), SessionManager.GetSessionId(), FirstSessionId);

	Now += FTimespan(1);
	const FString RotatedSessionId = SessionManager.GetSessionId();
	TestNotEqual(TEXT("Different session id one tick past the boundary"), RotatedSessionId, FirstSessionId);
	TestFalse(TEXT("Rotated session id is non-empty"), RotatedSessionId.IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogSessionManagerMaxDurationBoundaryTest, "UnrealHog.Session.SessionManager.RotatesOnlyStrictlyAfterMaxDurationBoundary", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogSessionManagerMaxDurationBoundaryTest::RunTest(const FString& Parameters)
{
	FDateTime Now(2026, 1, 1, 0, 0, 0);
	int32 Counter = 0;

	FPostHogSessionManager SessionManager([&Now]() { return Now; }, MakeCountingIdGenerator(Counter));
	SessionManager.SetCollectionPermitted(true);

	const FString FirstSessionId = SessionManager.GetSessionId();
	TestFalse(TEXT("First session id is non-empty"), FirstSessionId.IsEmpty());

	// Touch every 29 minutes (well under the 30-minute inactivity timeout) so only the
	// 24-hour max-duration boundary can trigger rotation. 49 iterations of 29 minutes
	// reaches 23h41m; the final advance below crosses the 24h line.
	for (int32 Index = 0; Index < 49; ++Index)
	{
		Now += FTimespan::FromMinutes(29.0);
		SessionManager.Touch();
	}

	Now = FDateTime(2026, 1, 1, 0, 0, 0) + FTimespan::FromHours(24.0);
	TestEqual(TEXT("Same session id at exactly the 24-hour boundary"), SessionManager.GetSessionId(), FirstSessionId);

	Now += FTimespan(1);
	const FString RotatedSessionId = SessionManager.GetSessionId();
	TestNotEqual(TEXT("Different session id one tick past the 24-hour boundary"), RotatedSessionId, FirstSessionId);
	TestFalse(TEXT("Rotated session id is non-empty"), RotatedSessionId.IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogSessionManagerBackgroundClearBoundaryTest, "UnrealHog.Session.SessionManager.ClearsOnlyStrictlyAfterInactivityBoundaryWhileBackgrounded", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogSessionManagerBackgroundClearBoundaryTest::RunTest(const FString& Parameters)
{
	FDateTime Now(2026, 1, 1, 0, 0, 0);
	int32 Counter = 0;

	FPostHogSessionManager SessionManager([&Now]() { return Now; }, MakeCountingIdGenerator(Counter));
	SessionManager.SetCollectionPermitted(true);

	const FString FirstSessionId = SessionManager.GetSessionId();
	TestFalse(TEXT("First session id is non-empty"), FirstSessionId.IsEmpty());

	SessionManager.OnBackground();

	Now += FTimespan::FromMinutes(30.0);
	TestEqual(TEXT("Session id retained at exactly the 30-minute boundary while backgrounded"), SessionManager.GetSessionId(), FirstSessionId);

	Now += FTimespan(1);
	TestTrue(TEXT("Session cleared one tick past the boundary while backgrounded"), SessionManager.GetSessionId().IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogSessionManagerForegroundAfterExpiryStartsFreshTest, "UnrealHog.Session.SessionManager.ForegroundAfterExpiryStartsFreshSession", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogSessionManagerForegroundAfterExpiryStartsFreshTest::RunTest(const FString& Parameters)
{
	FDateTime Now(2026, 1, 1, 0, 0, 0);
	int32 Counter = 0;

	FPostHogSessionManager SessionManager([&Now]() { return Now; }, MakeCountingIdGenerator(Counter));
	SessionManager.SetCollectionPermitted(true);

	const FString FirstSessionId = SessionManager.GetSessionId();
	TestFalse(TEXT("First session id is non-empty"), FirstSessionId.IsEmpty());

	SessionManager.OnBackground();
	Now += FTimespan::FromMinutes(30.0) + FTimespan(1);

	SessionManager.OnForeground();
	const FString FreshSessionId = SessionManager.GetSessionId();

	TestFalse(TEXT("Fresh session id is non-empty"), FreshSessionId.IsEmpty());
	TestNotEqual(TEXT("Fresh session id differs from the original"), FreshSessionId, FirstSessionId);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogSessionManagerBackgroundedWithNoActiveSessionTest, "UnrealHog.Session.SessionManager.BackgroundedWithNoActiveSessionReturnsEmpty", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogSessionManagerBackgroundedWithNoActiveSessionTest::RunTest(const FString& Parameters)
{
	FDateTime Now(2026, 1, 1, 0, 0, 0);
	int32 Counter = 0;

	FPostHogSessionManager SessionManager([&Now]() { return Now; }, MakeCountingIdGenerator(Counter));
	SessionManager.SetCollectionPermitted(true);

	SessionManager.OnBackground();

	TestTrue(TEXT("No session id while backgrounded with no active session"), SessionManager.GetSessionId().IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogSessionManagerStartNewSessionRequiresConsentTest, "UnrealHog.Session.SessionManager.StartNewSessionWithoutConsentStaysEmpty", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogSessionManagerStartNewSessionRequiresConsentTest::RunTest(const FString& Parameters)
{
	FDateTime Now(2026, 1, 1, 0, 0, 0);
	int32 Counter = 0;

	FPostHogSessionManager SessionManager([&Now]() { return Now; }, MakeCountingIdGenerator(Counter));

	SessionManager.StartNewSession();

	TestTrue(TEXT("No session id generated before collection is permitted"), SessionManager.GetSessionId().IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogSessionManagerExplicitStartNewSessionRotatesTest, "UnrealHog.Session.SessionManager.ExplicitStartNewSessionRotatesMidActiveWindow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogSessionManagerExplicitStartNewSessionRotatesTest::RunTest(const FString& Parameters)
{
	FDateTime Now(2026, 1, 1, 0, 0, 0);
	int32 Counter = 0;

	FPostHogSessionManager SessionManager([&Now]() { return Now; }, MakeCountingIdGenerator(Counter));
	SessionManager.SetCollectionPermitted(true);

	const FString FirstSessionId = SessionManager.GetSessionId();
	TestFalse(TEXT("First session id is non-empty"), FirstSessionId.IsEmpty());

	Now += FTimespan::FromMinutes(5.0);
	SessionManager.StartNewSession();
	const FString NewSessionId = SessionManager.GetSessionId();

	TestFalse(TEXT("New session id is non-empty"), NewSessionId.IsEmpty());
	TestNotEqual(TEXT("Explicit new session differs from the original"), NewSessionId, FirstSessionId);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogSessionManagerSharedIdWithinActiveWindowTest, "UnrealHog.Session.SessionManager.SharesOneSessionIdWithinActiveWindow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogSessionManagerSharedIdWithinActiveWindowTest::RunTest(const FString& Parameters)
{
	FDateTime Now(2026, 1, 1, 0, 0, 0);
	int32 Counter = 0;

	FPostHogSessionManager SessionManager([&Now]() { return Now; }, MakeCountingIdGenerator(Counter));
	SessionManager.SetCollectionPermitted(true);

	const FString FirstSessionId = SessionManager.GetSessionId();
	TestFalse(TEXT("First session id is non-empty"), FirstSessionId.IsEmpty());
	SessionManager.Touch();

	Now += FTimespan::FromMinutes(5.0);
	TestEqual(TEXT("Same session id after five minutes"), SessionManager.GetSessionId(), FirstSessionId);
	SessionManager.Touch();

	Now += FTimespan::FromMinutes(5.0);
	TestEqual(TEXT("Same session id after ten total minutes"), SessionManager.GetSessionId(), FirstSessionId);
	SessionManager.Touch();

	Now += FTimespan::FromMinutes(5.0);
	TestEqual(TEXT("Same session id after fifteen total minutes"), SessionManager.GetSessionId(), FirstSessionId);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
