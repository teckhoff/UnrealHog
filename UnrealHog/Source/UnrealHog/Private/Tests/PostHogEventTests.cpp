#include "Events/PostHogEvent.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/DateTime.h"
#include "Containers/Set.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Events/PostHogEventProperties.h"

namespace
{
	bool IsCanonicalUuidV7(const FString& Value)
	{
		if (Value.Len() != 36)
		{
			return false;
		}

		FGuid ParsedGuid;
		if (!FGuid::ParseExact(Value, EGuidFormats::DigitsWithHyphensLower, ParsedGuid))
		{
			return false;
		}

		if (Value[14] != TCHAR('7'))
		{
			return false;
		}

		const TCHAR VariantChar = Value[19];
		return VariantChar == TCHAR('8') || VariantChar == TCHAR('9') || VariantChar == TCHAR('a') || VariantChar == TCHAR('b');
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventUuidTest, "UnrealHog.Events.Event.GeneratesCanonicalUuidV7", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventUuidTest::RunTest(const FString& Parameters)
{
	const FPostHogEvent Event(TEXT("test-event"), TEXT("distinct-1"));

	const FString EventId = Event.GetEventId();
	TestFalse(TEXT("Event ID non-empty"), EventId.IsEmpty());
	TestTrue(TEXT("Event ID is a canonical UUIDv7"), IsCanonicalUuidV7(EventId));

	const TSharedRef<FJsonObject> JsonObject = Event.ToJsonObject();
	FString JsonUuid;
	TestTrue(TEXT("JSON has uuid field"), JsonObject->TryGetStringField(TEXT("uuid"), JsonUuid));
	TestEqual(TEXT("JSON uuid matches GetEventId"), JsonUuid, EventId);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventTimestampTest, "UnrealHog.Events.Event.SetsTimestamp", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventTimestampTest::RunTest(const FString& Parameters)
{
	const FDateTime Before = FDateTime::UtcNow();
	const FPostHogEvent Event(TEXT("test-event"), TEXT("distinct-1"));
	const FDateTime After = FDateTime::UtcNow();

	const TSharedRef<FJsonObject> JsonObject = Event.ToJsonObject();
	FString TimestampString;
	TestTrue(TEXT("JSON has timestamp field"), JsonObject->TryGetStringField(TEXT("timestamp"), TimestampString));

	FDateTime ParsedTimestamp;
	TestTrue(TEXT("Timestamp parses as ISO 8601"), FDateTime::ParseIso8601(*TimestampString, ParsedTimestamp));

	const FTimespan Tolerance = FTimespan::FromSeconds(5.0);
	TestTrue(TEXT("Timestamp is within tolerance of construction window"), ParsedTimestamp >= Before - Tolerance && ParsedTimestamp <= After + Tolerance);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventDefaultPropertiesTest, "UnrealHog.Events.Event.EnrichesDefaultProperties", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventDefaultPropertiesTest::RunTest(const FString& Parameters)
{
	FPostHogEvent Event(TEXT("test-event"), TEXT("distinct-1"));
	Event.ApplySdkProperties(false);

	const TSharedRef<FJsonObject> JsonObject = Event.ToJsonObject();
	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("JSON has properties object"), JsonObject->TryGetObjectField(TEXT("properties"), PropertiesObject));

	FString LibValue;
	TestTrue(TEXT("properties has $lib"), (*PropertiesObject)->TryGetStringField(TEXT("$lib"), LibValue));
	FString LibVersionValue;
	TestTrue(TEXT("properties has $lib_version"), (*PropertiesObject)->TryGetStringField(TEXT("$lib_version"), LibVersionValue));

	TestFalse(TEXT("No arbitrary user-supplied field present"), (*PropertiesObject)->HasField(TEXT("some_custom_key")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventNameAndDistinctIdTest, "UnrealHog.Events.Event.PreservesNameAndDistinctId", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventNameAndDistinctIdTest::RunTest(const FString& Parameters)
{
	const FPostHogEvent Event(TEXT("my_event"), TEXT("distinct-42"));

	const TSharedRef<FJsonObject> JsonObject = Event.ToJsonObject();

	FString EventName;
	TestTrue(TEXT("JSON has event field"), JsonObject->TryGetStringField(TEXT("event"), EventName));
	TestEqual(TEXT("Event name preserved exactly"), EventName, TEXT("my_event"));

	FString DistinctId;
	TestTrue(TEXT("JSON has distinct_id field"), JsonObject->TryGetStringField(TEXT("distinct_id"), DistinctId));
	TestEqual(TEXT("Distinct ID preserved exactly"), DistinctId, TEXT("distinct-42"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventUniquePerEventTest, "UnrealHog.Events.Event.UuidIsUniquePerEvent", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventUniquePerEventTest::RunTest(const FString& Parameters)
{
	TSet<FString> SeenIds;

	for (int32 Index = 0; Index < 50; ++Index)
	{
		const FPostHogEvent Event(TEXT("test-event"), TEXT("distinct-1"));
		const FString EventId = Event.GetEventId();

		TestFalse(TEXT("Event ID is unique"), SeenIds.Contains(EventId));
		SeenIds.Add(EventId);
	}

	TestEqual(TEXT("All 50 event IDs are distinct"), SeenIds.Num(), 50);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventPropertiesAppliedTest, "UnrealHog.Events.EventProperties.AppliesStringNumberBoolean", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventPropertiesAppliedTest::RunTest(const FString& Parameters)
{
	FPostHogEvent Event(TEXT("test-event"), TEXT("distinct-1"));

	UPostHogEventProperties* Properties = NewObject<UPostHogEventProperties>();
	Properties->AddString(TEXT("string_key"), TEXT("string_value"));
	Properties->AddNumber(TEXT("number_key"), 42.5);
	Properties->AddBoolean(TEXT("bool_key"), true);

	Properties->ApplyToEvent(Event);

	const TSharedRef<FJsonObject> JsonObject = Event.ToJsonObject();
	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("JSON has properties object"), JsonObject->TryGetObjectField(TEXT("properties"), PropertiesObject));

	const TSharedPtr<FJsonValue> StringField = (*PropertiesObject)->TryGetField(TEXT("string_key"));
	TestTrue(TEXT("string_key exists"), StringField.IsValid());
	if (StringField.IsValid())
	{
		TestTrue(TEXT("string_key type"), StringField->Type == EJson::String);
		TestEqual(TEXT("string_key value"), StringField->AsString(), TEXT("string_value"));
	}

	const TSharedPtr<FJsonValue> NumberField = (*PropertiesObject)->TryGetField(TEXT("number_key"));
	TestTrue(TEXT("number_key exists"), NumberField.IsValid());
	if (NumberField.IsValid())
	{
		TestTrue(TEXT("number_key type"), NumberField->Type == EJson::Number);
		TestEqual(TEXT("number_key value"), NumberField->AsNumber(), 42.5);
	}

	const TSharedPtr<FJsonValue> BoolField = (*PropertiesObject)->TryGetField(TEXT("bool_key"));
	TestTrue(TEXT("bool_key exists"), BoolField.IsValid());
	if (BoolField.IsValid())
	{
		TestTrue(TEXT("bool_key type"), BoolField->Type == EJson::Boolean);
		TestTrue(TEXT("bool_key value"), BoolField->AsBool());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventPropertiesCopiedNotReferencedTest, "UnrealHog.Events.EventProperties.CopiedNotReferenced", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventPropertiesCopiedNotReferencedTest::RunTest(const FString& Parameters)
{
	FPostHogEvent Event(TEXT("test-event"), TEXT("distinct-1"));

	{
		UPostHogEventProperties* Properties = NewObject<UPostHogEventProperties>();
		Properties->AddString(TEXT("mutable_key"), TEXT("original_value"));
		Properties->ApplyToEvent(Event);

		// Mutate the source after applying; the event's stored value must not change.
		Properties->AddString(TEXT("mutable_key"), TEXT("mutated_value"));
	}

	const TSharedRef<FJsonObject> JsonObject = Event.ToJsonObject();
	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("JSON has properties object"), JsonObject->TryGetObjectField(TEXT("properties"), PropertiesObject));

	FString MutableKeyValue;
	TestTrue(TEXT("mutable_key exists"), (*PropertiesObject)->TryGetStringField(TEXT("mutable_key"), MutableKeyValue));
	TestEqual(TEXT("Event retains the value at time of Apply, unaffected by later source mutation"), MutableKeyValue, TEXT("original_value"));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
