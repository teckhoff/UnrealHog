#include "Events/PostHogEvent.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Events/PostHogEventProperties.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventPropertiesNullValueTest, "UnrealHog.Events.EventProperties.NullProperty", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventPropertiesNullValueTest::RunTest(const FString& Parameters)
{
	FPostHogEvent Event(TEXT("test-event"), TEXT("distinct-1"));

	UPostHogEventProperties* Properties = NewObject<UPostHogEventProperties>();
	Properties->AddNull(TEXT("null_key"));
	Properties->ApplyToEvent(Event);

	const TSharedRef<FJsonObject> JsonObject = Event.ToJsonObject();
	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("JSON has properties object"), JsonObject->TryGetObjectField(TEXT("properties"), PropertiesObject));

	const TSharedPtr<FJsonValue> NullField = (*PropertiesObject)->TryGetField(TEXT("null_key"));
	TestTrue(TEXT("null_key exists"), NullField.IsValid());
	if (NullField.IsValid())
	{
		TestTrue(TEXT("null_key type is Null"), NullField->Type == EJson::Null);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventPropertiesEmptyObjectTest, "UnrealHog.Events.EventProperties.EmptyObject", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventPropertiesEmptyObjectTest::RunTest(const FString& Parameters)
{
	FPostHogEvent Event(TEXT("test-event"), TEXT("distinct-1"));

	UPostHogEventProperties* Properties = NewObject<UPostHogEventProperties>();
	UPostHogEventProperties* EmptyChild = NewObject<UPostHogEventProperties>();
	Properties->AddObject(TEXT("empty_object_key"), EmptyChild);
	Properties->ApplyToEvent(Event);

	const TSharedRef<FJsonObject> JsonObject = Event.ToJsonObject();
	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("JSON has properties object"), JsonObject->TryGetObjectField(TEXT("properties"), PropertiesObject));

	const TSharedPtr<FJsonObject>* EmptyObjectField = nullptr;
	TestTrue(TEXT("empty_object_key exists and is an object"), (*PropertiesObject)->TryGetObjectField(TEXT("empty_object_key"), EmptyObjectField));
	if (EmptyObjectField)
	{
		TestEqual(TEXT("Empty object has no fields"), (*EmptyObjectField)->Values.Num(), 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventPropertiesEmptyArrayTest, "UnrealHog.Events.EventProperties.EmptyArray", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventPropertiesEmptyArrayTest::RunTest(const FString& Parameters)
{
	FPostHogEvent Event(TEXT("test-event"), TEXT("distinct-1"));

	UPostHogEventProperties* Properties = NewObject<UPostHogEventProperties>();
	UPostHogEventPropertyArray* EmptyArray = NewObject<UPostHogEventPropertyArray>();
	Properties->AddArray(TEXT("empty_array_key"), EmptyArray);
	Properties->ApplyToEvent(Event);

	const TSharedRef<FJsonObject> JsonObject = Event.ToJsonObject();
	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("JSON has properties object"), JsonObject->TryGetObjectField(TEXT("properties"), PropertiesObject));

	const TArray<TSharedPtr<FJsonValue>>* EmptyArrayField = nullptr;
	TestTrue(TEXT("empty_array_key exists and is an array"), (*PropertiesObject)->TryGetArrayField(TEXT("empty_array_key"), EmptyArrayField));
	if (EmptyArrayField)
	{
		TestEqual(TEXT("Empty array has no elements"), EmptyArrayField->Num(), 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventPropertiesNestedObjectTest, "UnrealHog.Events.EventProperties.NestedObject", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventPropertiesNestedObjectTest::RunTest(const FString& Parameters)
{
	FPostHogEvent Event(TEXT("test-event"), TEXT("distinct-1"));

	UPostHogEventProperties* Properties = NewObject<UPostHogEventProperties>();
	UPostHogEventProperties* Inner = NewObject<UPostHogEventProperties>();
	Inner->AddString(TEXT("leaf_key"), TEXT("leaf_value"));

	UPostHogEventProperties* Outer = NewObject<UPostHogEventProperties>();
	Outer->AddObject(TEXT("inner"), Inner);

	Properties->AddObject(TEXT("outer"), Outer);
	Properties->ApplyToEvent(Event);

	const TSharedRef<FJsonObject> JsonObject = Event.ToJsonObject();
	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("JSON has properties object"), JsonObject->TryGetObjectField(TEXT("properties"), PropertiesObject));

	const TSharedPtr<FJsonObject>* OuterObject = nullptr;
	TestTrue(TEXT("outer exists and is an object"), (*PropertiesObject)->TryGetObjectField(TEXT("outer"), OuterObject));
	if (OuterObject)
	{
		const TSharedPtr<FJsonObject>* InnerObject = nullptr;
		TestTrue(TEXT("inner exists and is an object"), (*OuterObject)->TryGetObjectField(TEXT("inner"), InnerObject));
		if (InnerObject)
		{
			FString LeafValue;
			TestTrue(TEXT("leaf_key exists"), (*InnerObject)->TryGetStringField(TEXT("leaf_key"), LeafValue));
			TestEqual(TEXT("leaf_key value round-trips"), LeafValue, TEXT("leaf_value"));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventPropertiesMixedArrayTest, "UnrealHog.Events.EventProperties.MixedArray", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventPropertiesMixedArrayTest::RunTest(const FString& Parameters)
{
	FPostHogEvent Event(TEXT("test-event"), TEXT("distinct-1"));

	UPostHogEventProperties* NestedObject = NewObject<UPostHogEventProperties>();
	NestedObject->AddString(TEXT("nested_key"), TEXT("nested_value"));

	UPostHogEventPropertyArray* MixedArray = NewObject<UPostHogEventPropertyArray>();
	MixedArray->AddString(TEXT("string_element"))
		->AddNumber(7.0)
		->AddBoolean(true)
		->AddNull()
		->AddObject(NestedObject);

	UPostHogEventProperties* Properties = NewObject<UPostHogEventProperties>();
	Properties->AddArray(TEXT("mixed_array_key"), MixedArray);
	Properties->ApplyToEvent(Event);

	const TSharedRef<FJsonObject> JsonObject = Event.ToJsonObject();
	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("JSON has properties object"), JsonObject->TryGetObjectField(TEXT("properties"), PropertiesObject));

	const TArray<TSharedPtr<FJsonValue>>* ArrayField = nullptr;
	TestTrue(TEXT("mixed_array_key exists and is an array"), (*PropertiesObject)->TryGetArrayField(TEXT("mixed_array_key"), ArrayField));
	if (ArrayField)
	{
		TestEqual(TEXT("Array has 5 elements in order"), ArrayField->Num(), 5);
		if (ArrayField->Num() == 5)
		{
			TestTrue(TEXT("Element 0 is a string"), (*ArrayField)[0]->Type == EJson::String);
			TestEqual(TEXT("Element 0 value"), (*ArrayField)[0]->AsString(), TEXT("string_element"));

			TestTrue(TEXT("Element 1 is a number"), (*ArrayField)[1]->Type == EJson::Number);
			TestEqual(TEXT("Element 1 value"), (*ArrayField)[1]->AsNumber(), 7.0);

			TestTrue(TEXT("Element 2 is a boolean"), (*ArrayField)[2]->Type == EJson::Boolean);
			TestTrue(TEXT("Element 2 value"), (*ArrayField)[2]->AsBool());

			TestTrue(TEXT("Element 3 is null"), (*ArrayField)[3]->Type == EJson::Null);

			TestTrue(TEXT("Element 4 is an object"), (*ArrayField)[4]->Type == EJson::Object);
			const TSharedPtr<FJsonObject> NestedJsonObject = (*ArrayField)[4]->AsObject();
			TestTrue(TEXT("Element 4 is a valid object"), NestedJsonObject.IsValid());
			if (NestedJsonObject.IsValid())
			{
				FString NestedValue;
				TestTrue(TEXT("nested_key exists"), NestedJsonObject->TryGetStringField(TEXT("nested_key"), NestedValue));
				TestEqual(TEXT("nested_key value"), NestedValue, TEXT("nested_value"));
			}
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventPropertiesEmptyKeyChildSkippedTest, "UnrealHog.Events.EventProperties.EmptyKeyChildSkipped", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventPropertiesEmptyKeyChildSkippedTest::RunTest(const FString& Parameters)
{
	FPostHogEvent Event(TEXT("test-event"), TEXT("distinct-1"));

	UPostHogEventProperties* Inner = NewObject<UPostHogEventProperties>();
	Inner->AddString(TEXT(""), TEXT("should_be_dropped"));
	Inner->AddString(TEXT("sibling_key"), TEXT("sibling_value"));

	UPostHogEventProperties* Properties = NewObject<UPostHogEventProperties>();
	Properties->AddObject(TEXT("outer"), Inner);
	Properties->ApplyToEvent(Event);

	const TSharedRef<FJsonObject> JsonObject = Event.ToJsonObject();
	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("JSON has properties object"), JsonObject->TryGetObjectField(TEXT("properties"), PropertiesObject));

	const TSharedPtr<FJsonObject>* OuterObject = nullptr;
	TestTrue(TEXT("outer exists and is an object"), (*PropertiesObject)->TryGetObjectField(TEXT("outer"), OuterObject));
	if (OuterObject)
	{
		TestFalse(TEXT("Empty key entry is skipped"), (*OuterObject)->HasField(TEXT("")));

		FString SiblingValue;
		TestTrue(TEXT("Sibling key survives"), (*OuterObject)->TryGetStringField(TEXT("sibling_key"), SiblingValue));
		TestEqual(TEXT("Sibling key value round-trips"), SiblingValue, TEXT("sibling_value"));

		TestEqual(TEXT("Only the sibling key remains"), (*OuterObject)->Values.Num(), 1);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventPropertiesRichValueSourceMutationTest, "UnrealHog.Events.EventProperties.SourceMutationDoesNotAffectCapturedEvent", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventPropertiesRichValueSourceMutationTest::RunTest(const FString& Parameters)
{
	FPostHogEvent Event(TEXT("test-event"), TEXT("distinct-1"));

	UPostHogEventProperties* Properties = NewObject<UPostHogEventProperties>();

	{
		UPostHogEventProperties* ChildObject = NewObject<UPostHogEventProperties>();
		ChildObject->AddString(TEXT("child_key"), TEXT("original_value"));

		UPostHogEventPropertyArray* ChildArray = NewObject<UPostHogEventPropertyArray>();
		ChildArray->AddString(TEXT("original_element"));

		Properties->AddObject(TEXT("object_key"), ChildObject);
		Properties->AddArray(TEXT("array_key"), ChildArray);
		Properties->ApplyToEvent(Event);

		// Mutate the source builders after capture; the event's stored value must not change.
		ChildObject->AddString(TEXT("child_key"), TEXT("mutated_value"));
		ChildArray->AddString(TEXT("mutated_element"));

		// Destroy the source builders; the captured event holds an independent value copy.
		ChildObject->MarkAsGarbage();
		ChildArray->MarkAsGarbage();
	}

	const TSharedRef<FJsonObject> JsonObject = Event.ToJsonObject();
	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("JSON has properties object"), JsonObject->TryGetObjectField(TEXT("properties"), PropertiesObject));

	const TSharedPtr<FJsonObject>* ObjectField = nullptr;
	TestTrue(TEXT("object_key exists and is an object"), (*PropertiesObject)->TryGetObjectField(TEXT("object_key"), ObjectField));
	if (ObjectField)
	{
		FString ChildValue;
		TestTrue(TEXT("child_key exists"), (*ObjectField)->TryGetStringField(TEXT("child_key"), ChildValue));
		TestEqual(TEXT("Captured object value is unaffected by later source mutation"), ChildValue, TEXT("original_value"));
	}

	const TArray<TSharedPtr<FJsonValue>>* ArrayField = nullptr;
	TestTrue(TEXT("array_key exists and is an array"), (*PropertiesObject)->TryGetArrayField(TEXT("array_key"), ArrayField));
	if (ArrayField)
	{
		TestEqual(TEXT("Captured array retains only the original element"), ArrayField->Num(), 1);
		if (ArrayField->Num() == 1)
		{
			TestEqual(TEXT("Captured array element is unaffected by later source mutation"), (*ArrayField)[0]->AsString(), TEXT("original_element"));
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
