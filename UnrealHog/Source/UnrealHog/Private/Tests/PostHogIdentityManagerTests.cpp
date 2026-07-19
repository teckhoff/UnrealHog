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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogIdentityManagerIdentifyFirstLinksAnonymousTest, "UnrealHog.Identity.IdentityManager.IdentifyFirstCallReturnsPriorAnonymousId", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogIdentityManagerIdentifyFirstLinksAnonymousTest::RunTest(const FString& Parameters)
{
	FPostHogInMemoryStorageProvider Storage;
	FPostHogIdentityManager Manager;
	Manager.LoadOrCreate(Storage);
	const FString AnonymousId = Manager.GetAnonymousId();

	const FString PreviousAnonId = Manager.Identify(TEXT("user-1"), Storage);

	TestEqual(TEXT("First identify returns the prior anonymous id"), PreviousAnonId, AnonymousId);
	TestTrue(TEXT("Manager is identified after first identify"), Manager.IsIdentified());
	TestEqual(TEXT("Effective distinct id is the new distinct id"), Manager.GetEffectiveDistinctId(), FString(TEXT("user-1")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogIdentityManagerIdentifySecondCallDoesNotRelinkTest, "UnrealHog.Identity.IdentityManager.IdentifySecondCallReturnsEmptyNoRelink", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogIdentityManagerIdentifySecondCallDoesNotRelinkTest::RunTest(const FString& Parameters)
{
	FPostHogInMemoryStorageProvider Storage;
	FPostHogIdentityManager Manager;
	Manager.LoadOrCreate(Storage);

	Manager.Identify(TEXT("user-1"), Storage);
	const FString SecondPreviousAnonId = Manager.Identify(TEXT("user-2"), Storage);

	TestTrue(TEXT("Second identify does not return an anonymous id to relink"), SecondPreviousAnonId.IsEmpty());
	TestEqual(TEXT("Effective distinct id is the latest identified id"), Manager.GetEffectiveDistinctId(), FString(TEXT("user-2")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogIdentityManagerResetRegeneratesAnonymousIdTest, "UnrealHog.Identity.IdentityManager.ResetWithoutReuseRegeneratesAnonymousIdAndClearsState", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogIdentityManagerResetRegeneratesAnonymousIdTest::RunTest(const FString& Parameters)
{
	FPostHogInMemoryStorageProvider Storage;
	int32 Counter = 0;
	FPostHogIdentityManager Manager(MakeCountingUuidGenerator(Counter));
	Manager.LoadOrCreate(Storage);
	const FString OriginalAnonymousId = Manager.GetAnonymousId();

	Manager.Identify(TEXT("user-1"), Storage);
	Manager.Reset(Storage, /*bReuseAnonymousId=*/false);

	TestNotEqual(TEXT("Anonymous id regenerated on reset"), Manager.GetAnonymousId(), OriginalAnonymousId);
	TestFalse(TEXT("No longer identified after reset"), Manager.IsIdentified());
	TestEqual(TEXT("Effective distinct id falls back to the new anonymous id"), Manager.GetEffectiveDistinctId(), Manager.GetAnonymousId());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogIdentityManagerResetReusesAnonymousIdTest, "UnrealHog.Identity.IdentityManager.ResetWithReuseKeepsAnonymousId", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogIdentityManagerResetReusesAnonymousIdTest::RunTest(const FString& Parameters)
{
	FPostHogInMemoryStorageProvider Storage;
	int32 Counter = 0;
	FPostHogIdentityManager Manager(MakeCountingUuidGenerator(Counter));
	Manager.LoadOrCreate(Storage);
	const FString OriginalAnonymousId = Manager.GetAnonymousId();

	Manager.Identify(TEXT("user-1"), Storage);
	Manager.Reset(Storage, /*bReuseAnonymousId=*/true);

	TestEqual(TEXT("Anonymous id unchanged on reset with reuse"), Manager.GetAnonymousId(), OriginalAnonymousId);
	TestFalse(TEXT("No longer identified after reset"), Manager.IsIdentified());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogIdentityManagerSetGroupBlankNoOpTest, "UnrealHog.Identity.IdentityManager.SetGroupBlankTypeOrKeyIsNoOp", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogIdentityManagerSetGroupBlankNoOpTest::RunTest(const FString& Parameters)
{
	FPostHogInMemoryStorageProvider Storage;
	FPostHogIdentityManager Manager;
	Manager.LoadOrCreate(Storage);

	FString StateJsonBefore;
	Storage.LoadState(TEXT("identity"), StateJsonBefore);

	TestFalse(TEXT("Blank group type rejected"), Manager.SetGroup(TEXT("  "), TEXT("acme"), Storage));
	TestFalse(TEXT("Blank group key rejected"), Manager.SetGroup(TEXT("company"), TEXT(""), Storage));

	TestEqual(TEXT("No group membership recorded"), Manager.GetGroups().Num(), 0);

	FString StateJsonAfter;
	Storage.LoadState(TEXT("identity"), StateJsonAfter);
	TestEqual(TEXT("No additional persistence occurred"), StateJsonAfter, StateJsonBefore);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogIdentityManagerSetGroupPersistsAcrossRestartTest, "UnrealHog.Identity.IdentityManager.SetGroupPersistsAcrossRestart", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogIdentityManagerSetGroupPersistsAcrossRestartTest::RunTest(const FString& Parameters)
{
	FPostHogInMemoryStorageProvider Storage;
	FPostHogIdentityManager FirstManager;
	FirstManager.LoadOrCreate(Storage);

	TestTrue(TEXT("SetGroup succeeds"), FirstManager.SetGroup(TEXT("company"), TEXT("acme"), Storage));

	FPostHogIdentityManager SecondManager;
	SecondManager.LoadOrCreate(Storage);

	const TMap<FString, FString> Groups = SecondManager.GetGroups();
	TestEqual(TEXT("Reloaded manager has one group"), Groups.Num(), 1);
	const FString* CompanyKey = Groups.Find(TEXT("company"));
	if (TestNotNull(TEXT("Reloaded manager has company group"), CompanyKey))
	{
		TestEqual(TEXT("Reloaded company group key is correct"), *CompanyKey, FString(TEXT("acme")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogIdentityManagerSetGroupRetainsOtherTypesTest, "UnrealHog.Identity.IdentityManager.SetGroupRetainsOtherGroupTypes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogIdentityManagerSetGroupRetainsOtherTypesTest::RunTest(const FString& Parameters)
{
	FPostHogInMemoryStorageProvider Storage;
	FPostHogIdentityManager Manager;
	Manager.LoadOrCreate(Storage);

	Manager.SetGroup(TEXT("company"), TEXT("acme"), Storage);
	Manager.SetGroup(TEXT("team"), TEXT("eng"), Storage);

	TMap<FString, FString> Groups = Manager.GetGroups();
	TestEqual(TEXT("Both group types retained"), Groups.Num(), 2);
	TestEqual(TEXT("Company group unaffected by team update"), Groups.FindRef(TEXT("company")), FString(TEXT("acme")));
	TestEqual(TEXT("Team group set correctly"), Groups.FindRef(TEXT("team")), FString(TEXT("eng")));

	Manager.SetGroup(TEXT("company"), TEXT("other-corp"), Storage);
	Groups = Manager.GetGroups();
	TestEqual(TEXT("Still two group types after replacement"), Groups.Num(), 2);
	TestEqual(TEXT("Company group replaced in place"), Groups.FindRef(TEXT("company")), FString(TEXT("other-corp")));
	TestEqual(TEXT("Team group untouched by company replacement"), Groups.FindRef(TEXT("team")), FString(TEXT("eng")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogIdentityManagerClearGroupsTest, "UnrealHog.Identity.IdentityManager.ClearGroupsEmptiesAndPersists", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogIdentityManagerClearGroupsTest::RunTest(const FString& Parameters)
{
	FPostHogInMemoryStorageProvider Storage;
	FPostHogIdentityManager Manager;
	Manager.LoadOrCreate(Storage);
	Manager.SetGroup(TEXT("company"), TEXT("acme"), Storage);

	Manager.ClearGroups(Storage);

	TestEqual(TEXT("Groups empty after ClearGroups"), Manager.GetGroups().Num(), 0);

	FPostHogIdentityManager ReloadedManager;
	ReloadedManager.LoadOrCreate(Storage);
	TestEqual(TEXT("Cleared groups persisted across restart"), ReloadedManager.GetGroups().Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogIdentityManagerResetClearsGroupsTest, "UnrealHog.Identity.IdentityManager.ResetClearsGroups", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogIdentityManagerResetClearsGroupsTest::RunTest(const FString& Parameters)
{
	FPostHogInMemoryStorageProvider Storage;
	FPostHogIdentityManager Manager;
	Manager.LoadOrCreate(Storage);
	Manager.SetGroup(TEXT("company"), TEXT("acme"), Storage);

	Manager.Reset(Storage, /*bReuseAnonymousId=*/false);

	TestEqual(TEXT("Groups empty after Reset"), Manager.GetGroups().Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogIdentityManagerGetGroupsIsDeepCopyTest, "UnrealHog.Identity.IdentityManager.GetGroupsReturnsDeepCopy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogIdentityManagerGetGroupsIsDeepCopyTest::RunTest(const FString& Parameters)
{
	FPostHogInMemoryStorageProvider Storage;
	FPostHogIdentityManager Manager;
	Manager.LoadOrCreate(Storage);
	Manager.SetGroup(TEXT("company"), TEXT("acme"), Storage);

	TMap<FString, FString> Groups = Manager.GetGroups();
	Groups.Add(TEXT("team"), TEXT("eng"));
	Groups.Remove(TEXT("company"));

	TestEqual(TEXT("Manager state unaffected by mutating returned map"), Manager.GetGroups().Num(), 1);
	TestEqual(TEXT("Manager still has original company group"), Manager.GetGroups().FindRef(TEXT("company")), FString(TEXT("acme")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
