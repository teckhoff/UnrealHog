// Trevor Eckhoff, 2026. All rights reserved.

#include "Events/PostHogBatchPayload.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Events/PostHogEvent.h"
#include "Events/PostHogEventProperties.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventJsonBasicTest, "UnrealHog.Events.Json.EventReturnsValidJson", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventJsonBasicTest::RunTest(const FString& Parameters)
{
	const FPostHogEvent Event(TEXT("basic_event"), TEXT("distinct-1"));
	const TSharedRef<FJsonObject> JsonObject = Event.ToJsonObject();

	TestTrue(TEXT("Has uuid"), JsonObject->HasTypedField<EJson::String>(TEXT("uuid")));
	TestTrue(TEXT("Has event"), JsonObject->HasTypedField<EJson::String>(TEXT("event")));
	TestTrue(TEXT("Has distinct_id"), JsonObject->HasTypedField<EJson::String>(TEXT("distinct_id")));
	TestTrue(TEXT("Has timestamp"), JsonObject->HasTypedField<EJson::String>(TEXT("timestamp")));
	TestTrue(TEXT("Has properties object"), JsonObject->HasTypedField<EJson::Object>(TEXT("properties")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventJsonWithPropertiesTest, "UnrealHog.Events.Json.EventIncludesProperties", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventJsonWithPropertiesTest::RunTest(const FString& Parameters)
{
	FPostHogEvent Event(TEXT("event_with_props"), TEXT("distinct-1"));

	UPostHogEventProperties* Properties = NewObject<UPostHogEventProperties>();
	Properties->AddString(TEXT("plan"), TEXT("pro"));
	Properties->AddNumber(TEXT("seats"), 5);
	Properties->ApplyToEvent(Event);

	const TSharedRef<FJsonObject> JsonObject = Event.ToJsonObject();
	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("Has properties object"), JsonObject->TryGetObjectField(TEXT("properties"), PropertiesObject));

	FString PlanValue;
	TestTrue(TEXT("Has plan field"), (*PropertiesObject)->TryGetStringField(TEXT("plan"), PlanValue));
	TestEqual(TEXT("plan value"), PlanValue, TEXT("pro"));

	double SeatsValue = 0.0;
	TestTrue(TEXT("Has seats field"), (*PropertiesObject)->TryGetNumberField(TEXT("seats"), SeatsValue));
	TestEqual(TEXT("seats value"), SeatsValue, 5.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogBatchJsonEmptyTest, "UnrealHog.Events.Json.EmptyBatchReturnsValidJson", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogBatchJsonEmptyTest::RunTest(const FString& Parameters)
{
	const FPostHogBatchPayload Payload(TEXT("phc_test_key"));

	TestEqual(TEXT("Num is zero"), Payload.Num(), 0);

	const TSharedRef<FJsonObject> JsonObject = Payload.ToJsonObject();

	FString ApiKey;
	TestTrue(TEXT("Has api_key"), JsonObject->TryGetStringField(TEXT("api_key"), ApiKey));
	TestEqual(TEXT("api_key value"), ApiKey, TEXT("phc_test_key"));

	FString SentAt;
	TestTrue(TEXT("Has sent_at"), JsonObject->TryGetStringField(TEXT("sent_at"), SentAt));
	TestFalse(TEXT("sent_at non-empty"), SentAt.IsEmpty());

	const TArray<TSharedPtr<FJsonValue>>* BatchArray = nullptr;
	TestTrue(TEXT("Has batch array"), JsonObject->TryGetArrayField(TEXT("batch"), BatchArray));
	TestEqual(TEXT("batch array is empty"), BatchArray->Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogBatchJsonWithEventsTest, "UnrealHog.Events.Json.BatchIncludesEvents", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogBatchJsonWithEventsTest::RunTest(const FString& Parameters)
{
	FPostHogBatchPayload Payload(TEXT("phc_test_key"));
	Payload.AddEvent(FPostHogEvent(TEXT("event_one"), TEXT("distinct-1")));
	Payload.AddEvent(FPostHogEvent(TEXT("event_two"), TEXT("distinct-2")));
	Payload.AddEvent(FPostHogEvent(TEXT("event_three"), TEXT("distinct-3")));

	TestEqual(TEXT("Num matches added events"), Payload.Num(), 3);

	const TSharedRef<FJsonObject> JsonObject = Payload.ToJsonObject();
	const TArray<TSharedPtr<FJsonValue>>* BatchArray = nullptr;
	TestTrue(TEXT("Has batch array"), JsonObject->TryGetArrayField(TEXT("batch"), BatchArray));
	TestEqual(TEXT("batch array length matches"), BatchArray->Num(), 3);

	TArray<FString> EventNames;
	for (const TSharedPtr<FJsonValue>& Value : *BatchArray)
	{
		const TSharedPtr<FJsonObject> EventObject = Value->AsObject();
		TestTrue(TEXT("Batch entry is an object"), EventObject.IsValid());

		FString EventName;
		TestTrue(TEXT("Batch entry has event field"), EventObject->TryGetStringField(TEXT("event"), EventName));
		EventNames.Add(EventName);
	}

	TestTrue(TEXT("Contains event_one"), EventNames.Contains(TEXT("event_one")));
	TestTrue(TEXT("Contains event_two"), EventNames.Contains(TEXT("event_two")));
	TestTrue(TEXT("Contains event_three"), EventNames.Contains(TEXT("event_three")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
