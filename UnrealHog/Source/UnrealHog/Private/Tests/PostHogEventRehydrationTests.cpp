// Trevor Eckhoff, 2026. All rights reserved.

#include "Events/PostHogEventRehydration.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Events/PostHogBatchPayload.h"
#include "Events/PostHogEventQueue.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Storage/PostHogFileStorageProvider.h"
#include "Tests/PostHogFakeBatchTransport.h"

namespace
{
	class FScopedRehydrationTestStorageDirectory
	{
	public:
		FScopedRehydrationTestStorageDirectory()
			: RootPath(FPaths::Combine(FPlatformProcess::UserTempDir(), TEXT("UnrealHogTests"), FGuid::NewGuid().ToString(EGuidFormats::Digits)))
		{
		}

		~FScopedRehydrationTestStorageDirectory()
		{
			IFileManager::Get().DeleteDirectory(*RootPath, false, true);
		}

		const FString& GetRootPath() const { return RootPath; }

	private:
		FString RootPath;
	};

	FString GetFixturePath(const FString& FileName)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("UnrealHog"));
		check(Plugin.IsValid());

		return FPaths::Combine(Plugin->GetBaseDir(), TEXT("Source"), TEXT("UnrealHog"), TEXT("Private"), TEXT("Tests"), TEXT("Fixtures"), FileName);
	}

	bool ParseJsonObject(FAutomationTestBase& Test, const FString& Context, const FString& Json, TSharedPtr<FJsonObject>& OutObject)
	{
		OutObject.Reset();

		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, OutObject) || !OutObject.IsValid())
		{
			Test.AddError(FString::Printf(TEXT("%s: expected valid JSON object"), *Context));
			return false;
		}

		return true;
	}

	bool LoadFixture(FAutomationTestBase& Test, const FString& FileName, FString& OutJson, TSharedPtr<FJsonObject>& OutObject)
	{
		OutJson.Empty();
		OutObject.Reset();

		const FString FixturePath = GetFixturePath(FileName);
		if (!FFileHelper::LoadFileToString(OutJson, *FixturePath))
		{
			Test.AddError(FString::Printf(TEXT("Could not load fixture %s"), *FixturePath));
			return false;
		}

		return ParseJsonObject(Test, FileName, OutJson, OutObject);
	}

	bool TestJsonValuesEqual(FAutomationTestBase& Test, const FString& Context, const TSharedPtr<FJsonValue>& Expected, const TSharedPtr<FJsonValue>& Actual);

	bool TestJsonObjectsEqual(FAutomationTestBase& Test, const FString& Context, const FJsonObject& Expected, const FJsonObject& Actual)
	{
		bool bEqual = true;

		if (Expected.Values.Num() != Actual.Values.Num())
		{
			Test.AddError(FString::Printf(TEXT("%s: expected %d fields, got %d"), *Context, Expected.Values.Num(), Actual.Values.Num()));
			bEqual = false;
		}

		for (const auto& ExpectedField : Expected.Values)
		{
			const FString FieldName(ExpectedField.Key.ToView());
			const TSharedPtr<FJsonValue> ActualValue = Actual.TryGetField(ExpectedField.Key.ToView());
			if (!ActualValue.IsValid())
			{
				Test.AddError(FString::Printf(TEXT("%s: missing field '%s'"), *Context, *FieldName));
				bEqual = false;
				continue;
			}

			bEqual &= TestJsonValuesEqual(Test, FString::Printf(TEXT("%s.%s"), *Context, *FieldName), ExpectedField.Value, ActualValue);
		}

		return bEqual;
	}

	bool TestJsonArraysEqual(FAutomationTestBase& Test, const FString& Context, const TArray<TSharedPtr<FJsonValue>>& Expected, const TArray<TSharedPtr<FJsonValue>>& Actual)
	{
		bool bEqual = true;

		if (Expected.Num() != Actual.Num())
		{
			Test.AddError(FString::Printf(TEXT("%s: expected %d array entries, got %d"), *Context, Expected.Num(), Actual.Num()));
			bEqual = false;
		}

		const int32 SharedLength = FMath::Min(Expected.Num(), Actual.Num());
		for (int32 Index = 0; Index < SharedLength; ++Index)
		{
			bEqual &= TestJsonValuesEqual(Test, FString::Printf(TEXT("%s[%d]"), *Context, Index), Expected[Index], Actual[Index]);
		}

		return bEqual;
	}

	bool TestJsonValuesEqual(FAutomationTestBase& Test, const FString& Context, const TSharedPtr<FJsonValue>& Expected, const TSharedPtr<FJsonValue>& Actual)
	{
		if (!Expected.IsValid() || !Actual.IsValid())
		{
			Test.AddError(FString::Printf(TEXT("%s: compared invalid JSON value"), *Context));
			return false;
		}

		if (Expected->Type != Actual->Type)
		{
			Test.AddError(FString::Printf(TEXT("%s: expected JSON type %d, got %d"), *Context, static_cast<int32>(Expected->Type), static_cast<int32>(Actual->Type)));
			return false;
		}

		switch (Expected->Type)
		{
		case EJson::String:
			if (Expected->AsString() != Actual->AsString())
			{
				Test.AddError(FString::Printf(TEXT("%s: expected string '%s', got '%s'"), *Context, *Expected->AsString(), *Actual->AsString()));
				return false;
			}
			return true;

		case EJson::Number:
			if (Expected->AsNumber() != Actual->AsNumber())
			{
				Test.AddError(FString::Printf(TEXT("%s: expected number %.17g, got %.17g"), *Context, Expected->AsNumber(), Actual->AsNumber()));
				return false;
			}
			return true;

		case EJson::Boolean:
			if (Expected->AsBool() != Actual->AsBool())
			{
				Test.AddError(FString::Printf(TEXT("%s: expected bool %s, got %s"), *Context, Expected->AsBool() ? TEXT("true") : TEXT("false"), Actual->AsBool() ? TEXT("true") : TEXT("false")));
				return false;
			}
			return true;

		case EJson::Array:
			return TestJsonArraysEqual(Test, Context, Expected->AsArray(), Actual->AsArray());

		case EJson::Object:
			if (!Expected->AsObject().IsValid() || !Actual->AsObject().IsValid())
			{
				Test.AddError(FString::Printf(TEXT("%s: invalid JSON object value"), *Context));
				return false;
			}
			return TestJsonObjectsEqual(Test, Context, *Expected->AsObject(), *Actual->AsObject());

		case EJson::Null:
			return true;

		default:
			Test.AddError(FString::Printf(TEXT("%s: unsupported JSON type %d"), *Context, static_cast<int32>(Expected->Type)));
			return false;
		}
	}

	bool RunFixtureRehydrationTest(FAutomationTestBase& Test, const FString& FileName)
	{
		FString FixtureJson;
		TSharedPtr<FJsonObject> ExpectedObject;
		if (!LoadFixture(Test, FileName, FixtureJson, ExpectedObject))
		{
			return false;
		}

		const PostHogEventRehydration::FResult Result = PostHogEventRehydration::TryParsePersistedEventJson(FixtureJson);
		Test.TestTrue(TEXT("Fixture rehydration succeeds"), Result.IsSuccess());
		Test.TestTrue(TEXT("Successful rehydration has no diagnostic"), Result.Diagnostic.IsEmpty());
		if (!Result.IsSuccess())
		{
			return false;
		}

		const TSharedRef<FJsonObject> ActualObject = Result.Event.GetValue().ToJsonObject();
		return TestJsonObjectsEqual(Test, FileName, *ExpectedObject, *ActualObject);
	}

	TArray<TPair<FString, FString>> MakeInvalidPersistedEventCases()
	{
		TArray<TPair<FString, FString>> Cases;
		Cases.Emplace(TEXT("malformed json"), TEXT("{\"uuid\":"));
		Cases.Emplace(TEXT("missing uuid"), TEXT("{\"event\":\"event\",\"distinct_id\":\"distinct\",\"timestamp\":\"2026-07-15T07:43:12.107Z\",\"properties\":{}}"));
		Cases.Emplace(TEXT("missing event"), TEXT("{\"uuid\":\"018f9b2e-8d10-7c9c-8f3a-8ec0b8f29b3d\",\"distinct_id\":\"distinct\",\"timestamp\":\"2026-07-15T07:43:12.107Z\",\"properties\":{}}"));
		Cases.Emplace(TEXT("missing distinct_id"), TEXT("{\"uuid\":\"018f9b2e-8d10-7c9c-8f3a-8ec0b8f29b3d\",\"event\":\"event\",\"timestamp\":\"2026-07-15T07:43:12.107Z\",\"properties\":{}}"));
		Cases.Emplace(TEXT("missing timestamp"), TEXT("{\"uuid\":\"018f9b2e-8d10-7c9c-8f3a-8ec0b8f29b3d\",\"event\":\"event\",\"distinct_id\":\"distinct\",\"properties\":{}}"));
		Cases.Emplace(TEXT("missing properties"), TEXT("{\"uuid\":\"018f9b2e-8d10-7c9c-8f3a-8ec0b8f29b3d\",\"event\":\"event\",\"distinct_id\":\"distinct\",\"timestamp\":\"2026-07-15T07:43:12.107Z\"}"));
		Cases.Emplace(TEXT("empty uuid"), TEXT("{\"uuid\":\"\",\"event\":\"event\",\"distinct_id\":\"distinct\",\"timestamp\":\"2026-07-15T07:43:12.107Z\",\"properties\":{}}"));
		Cases.Emplace(TEXT("non-string uuid"), TEXT("{\"uuid\":123,\"event\":\"event\",\"distinct_id\":\"distinct\",\"timestamp\":\"2026-07-15T07:43:12.107Z\",\"properties\":{}}"));
		Cases.Emplace(TEXT("non-string event"), TEXT("{\"uuid\":\"018f9b2e-8d10-7c9c-8f3a-8ec0b8f29b3d\",\"event\":false,\"distinct_id\":\"distinct\",\"timestamp\":\"2026-07-15T07:43:12.107Z\",\"properties\":{}}"));
		Cases.Emplace(TEXT("non-string distinct_id"), TEXT("{\"uuid\":\"018f9b2e-8d10-7c9c-8f3a-8ec0b8f29b3d\",\"event\":\"event\",\"distinct_id\":{},\"timestamp\":\"2026-07-15T07:43:12.107Z\",\"properties\":{}}"));
		Cases.Emplace(TEXT("non-string timestamp"), TEXT("{\"uuid\":\"018f9b2e-8d10-7c9c-8f3a-8ec0b8f29b3d\",\"event\":\"event\",\"distinct_id\":\"distinct\",\"timestamp\":[],\"properties\":{}}"));
		Cases.Emplace(TEXT("non-object properties"), TEXT("{\"uuid\":\"018f9b2e-8d10-7c9c-8f3a-8ec0b8f29b3d\",\"event\":\"event\",\"distinct_id\":\"distinct\",\"timestamp\":\"2026-07-15T07:43:12.107Z\",\"properties\":[]}"));
		return Cases;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventRehydrationCurrentFixtureTest, "UnrealHog.Events.EventRehydration.ValidCurrentUuidPreservesJsonSemantics", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventRehydrationCurrentFixtureTest::RunTest(const FString& Parameters)
{
	return RunFixtureRehydrationTest(*this, TEXT("PersistedEventCurrentV7.json"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventRehydrationLegacyFixtureTest, "UnrealHog.Events.EventRehydration.ValidLegacyUuidPreservesJsonSemantics", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventRehydrationLegacyFixtureTest::RunTest(const FString& Parameters)
{
	return RunFixtureRehydrationTest(*this, TEXT("PersistedEventLegacyV4.json"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventRehydrationQueueLoadsMatchingStorageKeyTest, "UnrealHog.Events.EventRehydration.QueueLoadsStoredEventWithMatchingFilenameIntoBatch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventRehydrationQueueLoadsMatchingStorageKeyTest::RunTest(const FString& Parameters)
{
	FString FixtureJson;
	TSharedPtr<FJsonObject> ExpectedObject;
	if (!LoadFixture(*this, TEXT("PersistedEventCurrentV7.json"), FixtureJson, ExpectedObject))
	{
		return false;
	}

	const FString EventId = ExpectedObject->GetStringField(TEXT("uuid"));

	FScopedRehydrationTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Storage(Fixture.GetRootPath());
	FPostHogFakeBatchTransport Transport;
	TestTrue(TEXT("Fixture event saved"), Storage.SaveEvent(EventId, FixtureJson));

	FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 100, 100, 100);
	TestEqual(TEXT("Queue loaded the persisted event"), Queue.Num(), 1);

	Queue.Flush();
	TestEqual(TEXT("Flush sends loaded persisted event"), Transport.GetSentCount(), 1);
	TestEqual(TEXT("Batch contains one persisted event"), Transport.GetLastPayload().Num(), 1);

	const TSharedRef<FJsonObject> BatchPayload = Transport.GetLastPayload().ToJsonObject();
	const TArray<TSharedPtr<FJsonValue>>* BatchArray = nullptr;
	TestTrue(TEXT("Batch payload has batch array"), BatchPayload->TryGetArrayField(TEXT("batch"), BatchArray));
	if (!BatchArray || BatchArray->Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FJsonObject> BatchEvent = (*BatchArray)[0]->AsObject();
	TestTrue(TEXT("Batch entry is an object"), BatchEvent.IsValid());
	if (!BatchEvent.IsValid())
	{
		return false;
	}

	return TestJsonObjectsEqual(*this, TEXT("batch[0]"), *ExpectedObject, *BatchEvent);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventRehydrationQueueSkipsMismatchedStorageKeyTest, "UnrealHog.Events.EventRehydration.QueueSkipsMismatchedStorageKeyWithoutDeleting", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventRehydrationQueueSkipsMismatchedStorageKeyTest::RunTest(const FString& Parameters)
{
	FString FixtureJson;
	TSharedPtr<FJsonObject> ExpectedObject;
	if (!LoadFixture(*this, TEXT("PersistedEventCurrentV7.json"), FixtureJson, ExpectedObject))
	{
		return false;
	}

	FScopedRehydrationTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Storage(Fixture.GetRootPath());
	FPostHogFakeBatchTransport Transport;
	TestTrue(TEXT("Fixture event saved under mismatched key"), Storage.SaveEvent(TEXT("mismatched-storage-key"), FixtureJson));

	FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 100, 100, 100);
	TestEqual(TEXT("Mismatched persisted event is not queued"), Queue.Num(), 0);
	TestEqual(TEXT("Mismatched persisted event remains in storage"), Storage.GetEventCount(), 1);

	Queue.Flush();
	TestEqual(TEXT("No batch sent for mismatched persisted event"), Transport.GetSentCount(), 0);
	TestEqual(TEXT("Flush does not delete mismatched persisted event"), Storage.GetEventCount(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventRehydrationInvalidCasesFailTest, "UnrealHog.Events.EventRehydration.InvalidPersistedEventsFailWithoutReplacement", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventRehydrationInvalidCasesFailTest::RunTest(const FString& Parameters)
{
	const TArray<TPair<FString, FString>> InvalidCases = MakeInvalidPersistedEventCases();

	for (const TPair<FString, FString>& InvalidCase : InvalidCases)
	{
		const PostHogEventRehydration::FResult Result = PostHogEventRehydration::TryParsePersistedEventJson(InvalidCase.Value);
		TestFalse(*FString::Printf(TEXT("%s fails"), *InvalidCase.Key), Result.IsSuccess());
		TestFalse(*FString::Printf(TEXT("%s has diagnostic"), *InvalidCase.Key), Result.Diagnostic.IsEmpty());
	}

	FScopedRehydrationTestStorageDirectory Fixture;
	FPostHogFileStorageProvider Storage(Fixture.GetRootPath());
	FPostHogFakeBatchTransport Transport;

	for (int32 Index = 0; Index < InvalidCases.Num(); ++Index)
	{
		const FString EventId = FString::Printf(TEXT("invalid-%d"), Index);
		TestTrue(*FString::Printf(TEXT("Invalid event %d saved"), Index), Storage.SaveEvent(EventId, InvalidCases[Index].Value));
	}

	FPostHogEventQueue Queue(Storage, Transport, TEXT("test-api-key"), 100, 100, 100);
	TestEqual(TEXT("Invalid persisted events are not queued"), Queue.Num(), 0);
	TestEqual(TEXT("Invalid persisted events remain in storage"), Storage.GetEventCount(), InvalidCases.Num());

	Queue.Flush();
	TestEqual(TEXT("No batch sent for invalid persisted events"), Transport.GetSentCount(), 0);
	TestEqual(TEXT("Flush does not delete invalid persisted events"), Storage.GetEventCount(), InvalidCases.Num());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
