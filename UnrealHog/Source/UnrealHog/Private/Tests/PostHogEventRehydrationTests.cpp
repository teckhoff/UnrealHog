#include "Events/PostHogEvent.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Events/PostHogBatchPayload.h"

namespace
{
	bool JsonValuesEqual(const TSharedPtr<FJsonValue>& Left, const TSharedPtr<FJsonValue>& Right)
	{
		if (!Left.IsValid() || !Right.IsValid())
		{
			return Left.IsValid() == Right.IsValid();
		}

		if (Left->Type != Right->Type)
		{
			return false;
		}

		switch (Left->Type)
		{
		case EJson::Null:
			return true;
		case EJson::Boolean:
			return Left->AsBool() == Right->AsBool();
		case EJson::Number:
			return Left->AsNumber() == Right->AsNumber();
		case EJson::String:
			return Left->AsString() == Right->AsString();
		case EJson::Array:
			{
				const TArray<TSharedPtr<FJsonValue>>& LeftArray = Left->AsArray();
				const TArray<TSharedPtr<FJsonValue>>& RightArray = Right->AsArray();

				if (LeftArray.Num() != RightArray.Num())
				{
					return false;
				}

				for (int32 Index = 0; Index < LeftArray.Num(); ++Index)
				{
					if (!JsonValuesEqual(LeftArray[Index], RightArray[Index]))
					{
						return false;
					}
				}

				return true;
			}
		case EJson::Object:
			{
				const TSharedPtr<FJsonObject> LeftObject = Left->AsObject();
				const TSharedPtr<FJsonObject> RightObject = Right->AsObject();

				if (!LeftObject.IsValid() || !RightObject.IsValid())
				{
					return LeftObject.IsValid() == RightObject.IsValid();
				}

				if (LeftObject->Values.Num() != RightObject->Values.Num())
				{
					return false;
				}

				for (const auto& Pair : LeftObject->Values)
				{
					const TSharedPtr<FJsonValue>* RightValue = RightObject->Values.Find(Pair.Key);
					if (RightValue == nullptr)
					{
						return false;
					}

					if (!JsonValuesEqual(Pair.Value, *RightValue))
					{
						return false;
					}
				}

				return true;
			}
		default:
			return false;
		}
	}

	TSharedRef<FJsonObject> MakeFixtureProperties()
	{
		const TSharedRef<FJsonObject> Nested = MakeShared<FJsonObject>();
		Nested->SetStringField(TEXT("nested_key"), TEXT("nested_value"));

		TArray<TSharedPtr<FJsonValue>> ArrayValue;
		ArrayValue.Add(MakeShared<FJsonValueNumber>(1));
		ArrayValue.Add(MakeShared<FJsonValueString>(TEXT("two")));
		ArrayValue.Add(MakeShared<FJsonValueBoolean>(true));

		const TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
		Properties->SetObjectField(TEXT("nested_object"), Nested);
		Properties->SetArrayField(TEXT("array_value"), ArrayValue);
		Properties->SetField(TEXT("null_value"), MakeShared<FJsonValueNull>());
		Properties->SetNumberField(TEXT("number_value"), 42.5);
		Properties->SetBoolField(TEXT("bool_value"), false);
		Properties->SetStringField(TEXT("string_value"), TEXT("hello"));

		return Properties;
	}

	TSharedRef<FJsonObject> MakeFixtureEvent(const FString& Uuid, const FString& EventName, const FString& DistinctId, const FString& Timestamp)
	{
		const TSharedRef<FJsonObject> Fixture = MakeShared<FJsonObject>();
		Fixture->SetStringField(TEXT("uuid"), Uuid);
		Fixture->SetStringField(TEXT("event"), EventName);
		Fixture->SetStringField(TEXT("distinct_id"), DistinctId);
		Fixture->SetStringField(TEXT("timestamp"), Timestamp);
		Fixture->SetObjectField(TEXT("properties"), MakeFixtureProperties());
		return Fixture;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventRehydratePreservesCurrentUuidV7Test, "UnrealHog.Events.Event.Rehydrate.PreservesCurrentUuidV7Fixture", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventRehydratePreservesCurrentUuidV7Test::RunTest(const FString& Parameters)
{
	const FString FixtureUuid = TEXT("018f4b3a-7c2e-7a4d-8b6e-1234567890ab");
	const FString FixtureTimestamp = TEXT("2026-07-15T12:00:00.000Z");
	const TSharedRef<FJsonObject> Fixture = MakeFixtureEvent(FixtureUuid, TEXT("test_event"), TEXT("distinct-1"), FixtureTimestamp);

	FString ErrorMessage;
	const TOptional<FPostHogEvent> ParsedEvent = FPostHogEvent::TryParseFromJson(Fixture, ErrorMessage);

	TestTrue(TEXT("Parse succeeds"), ParsedEvent.IsSet());
	if (!ParsedEvent.IsSet())
	{
		return false;
	}

	TestEqual(TEXT("GetEventId matches fixture uuid"), ParsedEvent->GetEventId(), FixtureUuid);

	const TSharedRef<FJsonObject> RoundTripped = ParsedEvent->ToJsonObject();

	FString RoundTrippedEventName;
	TestTrue(TEXT("Round-tripped has event field"), RoundTripped->TryGetStringField(TEXT("event"), RoundTrippedEventName));
	TestEqual(TEXT("event unchanged"), RoundTrippedEventName, TEXT("test_event"));

	FString RoundTrippedDistinctId;
	TestTrue(TEXT("Round-tripped has distinct_id field"), RoundTripped->TryGetStringField(TEXT("distinct_id"), RoundTrippedDistinctId));
	TestEqual(TEXT("distinct_id unchanged"), RoundTrippedDistinctId, TEXT("distinct-1"));

	FString RoundTrippedTimestamp;
	TestTrue(TEXT("Round-tripped has timestamp field"), RoundTripped->TryGetStringField(TEXT("timestamp"), RoundTrippedTimestamp));
	TestEqual(TEXT("timestamp unchanged"), RoundTrippedTimestamp, FixtureTimestamp);

	const TSharedPtr<FJsonValue> FixtureProperties = Fixture->TryGetField(TEXT("properties"));
	const TSharedPtr<FJsonValue> RoundTrippedProperties = RoundTripped->TryGetField(TEXT("properties"));
	TestTrue(TEXT("Properties tree structurally unchanged"), JsonValuesEqual(FixtureProperties, RoundTrippedProperties));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventRehydratePreservesLegacyUuidV4Test, "UnrealHog.Events.Event.Rehydrate.PreservesLegacyUuidV4Fixture", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventRehydratePreservesLegacyUuidV4Test::RunTest(const FString& Parameters)
{
	const FString FixtureUuid = TEXT("550e8400-e29b-41d4-a716-446655440000");
	const FString FixtureTimestamp = TEXT("2020-01-01T00:00:00.000Z");
	const TSharedRef<FJsonObject> Fixture = MakeFixtureEvent(FixtureUuid, TEXT("legacy_event"), TEXT("distinct-legacy"), FixtureTimestamp);

	FString ErrorMessage;
	const TOptional<FPostHogEvent> ParsedEvent = FPostHogEvent::TryParseFromJson(Fixture, ErrorMessage);

	TestTrue(TEXT("Parse succeeds"), ParsedEvent.IsSet());
	if (!ParsedEvent.IsSet())
	{
		return false;
	}

	TestEqual(TEXT("Legacy v4 uuid preserved unchanged, not regenerated as v7"), ParsedEvent->GetEventId(), FixtureUuid);

	const TSharedRef<FJsonObject> RoundTripped = ParsedEvent->ToJsonObject();
	FString RoundTrippedUuid;
	TestTrue(TEXT("Round-tripped has uuid field"), RoundTripped->TryGetStringField(TEXT("uuid"), RoundTrippedUuid));
	TestEqual(TEXT("uuid unchanged"), RoundTrippedUuid, FixtureUuid);

	const TSharedPtr<FJsonValue> FixtureProperties = Fixture->TryGetField(TEXT("properties"));
	const TSharedPtr<FJsonValue> RoundTrippedProperties = RoundTripped->TryGetField(TEXT("properties"));
	TestTrue(TEXT("Properties tree structurally unchanged"), JsonValuesEqual(FixtureProperties, RoundTrippedProperties));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventRehydrateUuidIdenticalAcrossLoadAndBatchProjectionTest, "UnrealHog.Events.Event.Rehydrate.UuidIdenticalAcrossLoadAndBatchProjection", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventRehydrateUuidIdenticalAcrossLoadAndBatchProjectionTest::RunTest(const FString& Parameters)
{
	const FString FixtureUuid = TEXT("018f4b3a-7c2e-7a4d-8b6e-abcdef012345");
	const TSharedRef<FJsonObject> Fixture = MakeFixtureEvent(FixtureUuid, TEXT("batch_event"), TEXT("distinct-batch"), TEXT("2026-07-15T12:00:00.000Z"));

	FString ErrorMessage;
	const TOptional<FPostHogEvent> ParsedEvent = FPostHogEvent::TryParseFromJson(Fixture, ErrorMessage);

	TestTrue(TEXT("Parse succeeds"), ParsedEvent.IsSet());
	if (!ParsedEvent.IsSet())
	{
		return false;
	}

	TestEqual(TEXT("GetEventId matches fixture uuid (storage key parity)"), ParsedEvent->GetEventId(), FixtureUuid);

	const FPostHogBatchPayload Batch(TEXT("test-api-key"), TArray<FPostHogEvent>{ ParsedEvent.GetValue() });
	const TSharedRef<FJsonObject> BatchJson = Batch.ToJsonObject();

	const TArray<TSharedPtr<FJsonValue>>* BatchArray = nullptr;
	TestTrue(TEXT("Batch has batch array field"), BatchJson->TryGetArrayField(TEXT("batch"), BatchArray));
	if (BatchArray == nullptr || BatchArray->Num() != 1)
	{
		AddError(TEXT("Expected exactly one batch entry"));
		return false;
	}

	const TSharedPtr<FJsonObject>* BatchEntryObject = nullptr;
	TestTrue(TEXT("Batch entry is an object"), (*BatchArray)[0]->TryGetObject(BatchEntryObject));
	if (BatchEntryObject == nullptr)
	{
		return false;
	}

	FString BatchUuid;
	TestTrue(TEXT("Batch entry has uuid field"), (*BatchEntryObject)->TryGetStringField(TEXT("uuid"), BatchUuid));
	TestEqual(TEXT("Batch projection uuid matches parsed event uuid"), BatchUuid, FixtureUuid);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventRehydrateFailsOnMissingRequiredFieldTest, "UnrealHog.Events.Event.Rehydrate.FailsOnMissingRequiredField", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventRehydrateFailsOnMissingRequiredFieldTest::RunTest(const FString& Parameters)
{
	const TArray<FString> RequiredFields = { TEXT("uuid"), TEXT("event"), TEXT("distinct_id"), TEXT("timestamp"), TEXT("properties") };

	for (const FString& FieldToOmit : RequiredFields)
	{
		const TSharedRef<FJsonObject> Fixture = MakeFixtureEvent(TEXT("018f4b3a-7c2e-7a4d-8b6e-1234567890ab"), TEXT("test_event"), TEXT("distinct-1"), TEXT("2026-07-15T12:00:00.000Z"));
		Fixture->RemoveField(FieldToOmit);

		FString ErrorMessage;
		const TOptional<FPostHogEvent> ParsedEvent = FPostHogEvent::TryParseFromJson(Fixture, ErrorMessage);

		TestFalse(FString::Printf(TEXT("Parse fails when \"%s\" is missing"), *FieldToOmit), ParsedEvent.IsSet());
		TestFalse(FString::Printf(TEXT("Error message set when \"%s\" is missing"), *FieldToOmit), ErrorMessage.IsEmpty());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventRehydrateFailsOnMistypedRequiredFieldTest, "UnrealHog.Events.Event.Rehydrate.FailsOnMistypedRequiredField", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventRehydrateFailsOnMistypedRequiredFieldTest::RunTest(const FString& Parameters)
{
	const TArray<FString> StringFields = { TEXT("uuid"), TEXT("event"), TEXT("distinct_id"), TEXT("timestamp") };

	for (const FString& FieldToMistype : StringFields)
	{
		// FJsonValueNumber and FJsonValueBoolean both coerce successfully via TryGetString (e.g. to
		// "12345"/"true"), so they aren't valid mistyped fixtures here; an array is never coercible.
		const TSharedRef<FJsonObject> Fixture = MakeFixtureEvent(TEXT("018f4b3a-7c2e-7a4d-8b6e-1234567890ab"), TEXT("test_event"), TEXT("distinct-1"), TEXT("2026-07-15T12:00:00.000Z"));
		TArray<TSharedPtr<FJsonValue>> MistypedArray;
		MistypedArray.Add(MakeShared<FJsonValueString>(TEXT("unexpected")));
		Fixture->SetArrayField(FieldToMistype, MistypedArray);

		FString ErrorMessage;
		const TOptional<FPostHogEvent> ParsedEvent = FPostHogEvent::TryParseFromJson(Fixture, ErrorMessage);

		TestFalse(FString::Printf(TEXT("Parse fails when \"%s\" is an array instead of a string"), *FieldToMistype), ParsedEvent.IsSet());
		TestFalse(FString::Printf(TEXT("Error message set when \"%s\" is mistyped"), *FieldToMistype), ErrorMessage.IsEmpty());
	}

	{
		const TSharedRef<FJsonObject> Fixture = MakeFixtureEvent(TEXT("018f4b3a-7c2e-7a4d-8b6e-1234567890ab"), TEXT("test_event"), TEXT("distinct-1"), TEXT("2026-07-15T12:00:00.000Z"));
		Fixture->SetStringField(TEXT("properties"), TEXT("not_an_object"));

		FString ErrorMessage;
		const TOptional<FPostHogEvent> ParsedEvent = FPostHogEvent::TryParseFromJson(Fixture, ErrorMessage);

		TestFalse(TEXT("Parse fails when \"properties\" is a string instead of an object"), ParsedEvent.IsSet());
		TestFalse(TEXT("Error message set when \"properties\" is mistyped"), ErrorMessage.IsEmpty());
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
