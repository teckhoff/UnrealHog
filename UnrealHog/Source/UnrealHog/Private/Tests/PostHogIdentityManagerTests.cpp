#include "Identity/PostHogIdentityManager.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Serialization/JsonSerializer.h"
#include "Tests/PostHogInMemoryStorageProvider.h"

namespace
{
	bool IsValidUuidV7(const FString& Value)
	{
		FGuid ParsedGuid;
		if (!FGuid::ParseExact(Value, EGuidFormats::DigitsWithHyphensLower, ParsedGuid) || Value.Len() != 36)
		{
			return false;
		}
		return Value[14] == TCHAR('7');
	}

	FPostHogIdentityManager::FUuidGenerator MakeCountingUuidGenerator(int32& Counter)
	{
		return [&Counter]() { return FString::Printf(TEXT("uuid-%d"), ++Counter); };
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogIdentityManagerFirstLoadOrCreateTest, "UnrealHog.Identity.IdentityManager.FirstLoadOrCreateGeneratesAndPersistsOneAnonymousUuid", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogIdentityManagerFirstLoadOrCreateTest::RunTest(const FString& Parameters)
{
	FPostHogInMemoryStorageProvider Storage;
	FPostHogIdentityManager Manager;

	Manager.LoadOrCreate(Storage);

	TestTrue(TEXT("Generated anonymous id is a valid UUIDv7"), IsValidUuidV7(Manager.GetAnonymousId()));
	TestEqual(TEXT("Effective distinct id falls back to anonymous id"), Manager.GetEffectiveDistinctId(), Manager.GetAnonymousId());

	FString StateJson;
	TestTrue(TEXT("Identity state persisted under the 'identity' key"), Storage.LoadState(TEXT("identity"), StateJson));

	TSharedPtr<FJsonObject> StateObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(StateJson);
	TestTrue(TEXT("Persisted state parses as JSON"), FJsonSerializer::Deserialize(Reader, StateObject) && StateObject.IsValid());

	FString PersistedAnonymousId;
	TestTrue(TEXT("Persisted state has anonymous_id"), StateObject->TryGetStringField(TEXT("anonymous_id"), PersistedAnonymousId));
	TestEqual(TEXT("Persisted anonymous id matches in-memory value"), PersistedAnonymousId, Manager.GetAnonymousId());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogIdentityManagerReuseAcrossRestartTest, "UnrealHog.Identity.IdentityManager.SecondManagerReusesPersistedAnonymousId", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogIdentityManagerReuseAcrossRestartTest::RunTest(const FString& Parameters)
{
	FPostHogInMemoryStorageProvider Storage;

	FPostHogIdentityManager FirstManager;
	FirstManager.LoadOrCreate(Storage);
	const FString FirstAnonymousId = FirstManager.GetAnonymousId();

	FPostHogIdentityManager SecondManager;
	SecondManager.LoadOrCreate(Storage);

	TestEqual(TEXT("Second manager reuses the persisted anonymous id"), SecondManager.GetAnonymousId(), FirstAnonymousId);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogIdentityManagerMalformedStateTest, "UnrealHog.Identity.IdentityManager.MalformedStateReplacedWithFreshIdentityAndWarning", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogIdentityManagerMalformedStateTest::RunTest(const FString& Parameters)
{
	FPostHogInMemoryStorageProvider Storage;
	Storage.SaveState(TEXT("identity"), TEXT("{not valid json"));

	FPostHogIdentityManager Manager;

	AddExpectedError(TEXT("missing, malformed, or unsupported-version identity state"), EAutomationExpectedErrorFlags::Contains, 1);
	Manager.LoadOrCreate(Storage);

	TestTrue(TEXT("Fresh anonymous id generated"), IsValidUuidV7(Manager.GetAnonymousId()));

	FString StateJson;
	TestTrue(TEXT("Identity state re-persisted"), Storage.LoadState(TEXT("identity"), StateJson));
	TSharedPtr<FJsonObject> StateObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(StateJson);
	TestTrue(TEXT("Re-persisted state parses as well-formed JSON"), FJsonSerializer::Deserialize(Reader, StateObject) && StateObject.IsValid());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogIdentityManagerWrongVersionTest, "UnrealHog.Identity.IdentityManager.WrongVersionTreatedAsMalformed", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogIdentityManagerWrongVersionTest::RunTest(const FString& Parameters)
{
	FPostHogInMemoryStorageProvider Storage;

	const TSharedRef<FJsonObject> SeedState = MakeShared<FJsonObject>();
	SeedState->SetNumberField(TEXT("version"), 99);
	SeedState->SetStringField(TEXT("anonymous_id"), TEXT("old-anonymous-id"));
	Storage.SaveState(TEXT("identity"), SeedState);

	FPostHogIdentityManager Manager;

	AddExpectedError(TEXT("missing, malformed, or unsupported-version identity state"), EAutomationExpectedErrorFlags::Contains, 1);
	Manager.LoadOrCreate(Storage);

	TestNotEqual(TEXT("Anonymous id regenerated, not reused from unsupported version"), Manager.GetAnonymousId(), FString(TEXT("old-anonymous-id")));
	TestTrue(TEXT("Regenerated anonymous id is a valid UUIDv7"), IsValidUuidV7(Manager.GetAnonymousId()));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogIdentityManagerFallsBackToAnonymousTest, "UnrealHog.Identity.IdentityManager.GetEffectiveDistinctIdFallsBackToAnonymousIdWhenNotIdentified", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogIdentityManagerFallsBackToAnonymousTest::RunTest(const FString& Parameters)
{
	FPostHogInMemoryStorageProvider Storage;
	FPostHogIdentityManager Manager;

	Manager.LoadOrCreate(Storage);

	TestFalse(TEXT("Not identified by default"), Manager.IsIdentified());
	TestEqual(TEXT("Effective distinct id equals anonymous id"), Manager.GetEffectiveDistinctId(), Manager.GetAnonymousId());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogIdentityManagerPrefersDistinctIdTest, "UnrealHog.Identity.IdentityManager.GetEffectiveDistinctIdPrefersDistinctIdWhenSeeded", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogIdentityManagerPrefersDistinctIdTest::RunTest(const FString& Parameters)
{
	FPostHogInMemoryStorageProvider Storage;

	const TSharedRef<FJsonObject> SeedState = MakeShared<FJsonObject>();
	SeedState->SetNumberField(TEXT("version"), FPostHogIdentityManager::CurrentSchemaVersion);
	SeedState->SetStringField(TEXT("anonymous_id"), TEXT("seed-anonymous-id"));
	SeedState->SetStringField(TEXT("distinct_id"), TEXT("seed-distinct-id"));
	SeedState->SetBoolField(TEXT("is_identified"), true);
	Storage.SaveState(TEXT("identity"), SeedState);

	FPostHogIdentityManager Manager;
	Manager.LoadOrCreate(Storage);

	TestTrue(TEXT("Seeded state marks manager as identified"), Manager.IsIdentified());
	TestEqual(TEXT("Effective distinct id is the seeded distinct id"), Manager.GetEffectiveDistinctId(), FString(TEXT("seed-distinct-id")));
	TestEqual(TEXT("Anonymous id is still available"), Manager.GetAnonymousId(), FString(TEXT("seed-anonymous-id")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogIdentityManagerConstructorSideEffectFreeTest, "UnrealHog.Identity.IdentityManager.ConstructorDoesNotTouchStorageOrGenerateUuid", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogIdentityManagerConstructorSideEffectFreeTest::RunTest(const FString& Parameters)
{
	FPostHogInMemoryStorageProvider Storage;
	int32 Counter = 0;

	FPostHogIdentityManager Manager(MakeCountingUuidGenerator(Counter));

	TestEqual(TEXT("UUID generator not invoked by construction"), Counter, 0);
	TestEqual(TEXT("No events written by construction"), Storage.GetEventCount(), 0);

	FString UnusedStateJson;
	TestFalse(TEXT("No identity state written by construction"), Storage.LoadState(TEXT("identity"), UnusedStateJson));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
