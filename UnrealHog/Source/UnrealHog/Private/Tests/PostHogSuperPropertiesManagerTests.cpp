#include "SuperProperties/PostHogSuperPropertiesManager.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Events/PostHogEvent.h"
#include "Events/PostHogEventProperties.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonSerializer.h"
#include "Tests/PostHogInMemoryStorageProvider.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogSuperPropertiesManagerRegisterPersistsTest, "UnrealHog.SuperProperties.SuperPropertiesManager.RegisterPersistsUnderStateKeyWithVersion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogSuperPropertiesManagerRegisterPersistsTest::RunTest(const FString& Parameters)
{
	FPostHogInMemoryStorageProvider Storage;
	FPostHogSuperPropertiesManager Manager;
	Manager.LoadOrCreate(Storage);

	FPostHogEventProperty Value;
	Value.Type = EPostHogPropertyType::String;
	Value.StringValue = TEXT("bar");
	Manager.Register(TEXT("foo"), Value, Storage);

	TestEqual(TEXT("One property registered"), Manager.Num(), 1);

	FString StateJson;
	TestTrue(TEXT("Super properties persisted under 'super_properties' key"), Storage.LoadState(TEXT("super_properties"), StateJson));

	TSharedPtr<FJsonObject> StateObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(StateJson);
	TestTrue(TEXT("Persisted state parses as JSON"), FJsonSerializer::Deserialize(Reader, StateObject) && StateObject.IsValid());

	int32 Version = 0;
	TestTrue(TEXT("Persisted state has version field"), StateObject->TryGetNumberField(TEXT("version"), Version));
	TestEqual(TEXT("Version matches current schema"), Version, FPostHogSuperPropertiesManager::CurrentSchemaVersion);

	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("Persisted state has properties object"), StateObject->TryGetObjectField(TEXT("properties"), PropertiesObject));

	FString FooValue;
	TestTrue(TEXT("properties has foo"), (*PropertiesObject)->TryGetStringField(TEXT("foo"), FooValue));
	TestEqual(TEXT("foo value round-trips"), FooValue, FString(TEXT("bar")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogSuperPropertiesManagerRestartPersistenceTest, "UnrealHog.SuperProperties.SuperPropertiesManager.SecondManagerReusesPersistedProperties", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogSuperPropertiesManagerRestartPersistenceTest::RunTest(const FString& Parameters)
{
	FPostHogInMemoryStorageProvider Storage;

	FPostHogSuperPropertiesManager FirstManager;
	FirstManager.LoadOrCreate(Storage);

	FPostHogEventProperty Value;
	Value.Type = EPostHogPropertyType::String;
	Value.StringValue = TEXT("bar");
	FirstManager.Register(TEXT("foo"), Value, Storage);

	FPostHogSuperPropertiesManager SecondManager;
	SecondManager.LoadOrCreate(Storage);

	TestEqual(TEXT("Second manager loads the registered property"), SecondManager.Num(), 1);

	FPostHogEvent Event(TEXT("test-event"), TEXT("distinct-1"));
	SecondManager.ApplyTo(Event);

	const TSharedRef<FJsonObject> JsonObject = Event.ToJsonObject();
	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("JSON has properties object"), JsonObject->TryGetObjectField(TEXT("properties"), PropertiesObject));

	FString FooValue;
	TestTrue(TEXT("properties has foo"), (*PropertiesObject)->TryGetStringField(TEXT("foo"), FooValue));
	TestEqual(TEXT("foo value round-trips across manager instances"), FooValue, FString(TEXT("bar")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogSuperPropertiesManagerBlankKeyRejectedTest, "UnrealHog.SuperProperties.SuperPropertiesManager.RegisterBlankKeyIsNoOpWithWarning", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogSuperPropertiesManagerBlankKeyRejectedTest::RunTest(const FString& Parameters)
{
	FPostHogInMemoryStorageProvider Storage;
	FPostHogSuperPropertiesManager Manager;
	Manager.LoadOrCreate(Storage);

	FPostHogEventProperty Value;
	Value.Type = EPostHogPropertyType::String;
	Value.StringValue = TEXT("ignored");

	AddExpectedError(TEXT("empty or whitespace-only key"), EAutomationExpectedErrorFlags::Contains, 1);
	Manager.Register(TEXT("   "), Value, Storage);

	TestEqual(TEXT("Blank key registration is a no-op"), Manager.Num(), 0);

	FString StateJson;
	TestFalse(TEXT("No state persisted for a rejected registration"), Storage.LoadState(TEXT("super_properties"), StateJson));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogSuperPropertiesManagerReservedKeyRejectedTest, "UnrealHog.SuperProperties.SuperPropertiesManager.RegisterReservedKeyIsRejected", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogSuperPropertiesManagerReservedKeyRejectedTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("register protected PostHog property \"$session_id\" as a super property"), EAutomationExpectedErrorFlags::Contains, 1, false);

	FPostHogInMemoryStorageProvider Storage;
	FPostHogSuperPropertiesManager Manager;
	Manager.LoadOrCreate(Storage);

	FPostHogEventProperty Value;
	Value.Type = EPostHogPropertyType::String;
	Value.StringValue = TEXT("ignored");
	Manager.Register(TEXT("$session_id"), Value, Storage);

	TestEqual(TEXT("Reserved key registration is a no-op"), Manager.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogSuperPropertiesManagerMalformedStateTest, "UnrealHog.SuperProperties.SuperPropertiesManager.MalformedStateLeavesEmptyAndDoesNotOverwrite", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogSuperPropertiesManagerMalformedStateTest::RunTest(const FString& Parameters)
{
	FPostHogInMemoryStorageProvider Storage;
	const FString MalformedJson = TEXT("{not valid json");
	Storage.SaveState(TEXT("super_properties"), MalformedJson);

	FPostHogSuperPropertiesManager Manager;

	AddExpectedError(TEXT("missing, malformed, or unsupported-version super property state"), EAutomationExpectedErrorFlags::Contains, 1);
	Manager.LoadOrCreate(Storage);

	TestEqual(TEXT("No super properties loaded from malformed state"), Manager.Num(), 0);

	FString StateJson;
	TestTrue(TEXT("Original malformed state still present"), Storage.LoadState(TEXT("super_properties"), StateJson));
	TestEqual(TEXT("Storage was not rewritten"), StateJson, MalformedJson);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogSuperPropertiesManagerWrongVersionTest, "UnrealHog.SuperProperties.SuperPropertiesManager.WrongVersionTreatedAsMalformed", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogSuperPropertiesManagerWrongVersionTest::RunTest(const FString& Parameters)
{
	FPostHogInMemoryStorageProvider Storage;

	const TSharedRef<FJsonObject> SeedState = MakeShared<FJsonObject>();
	SeedState->SetNumberField(TEXT("version"), 99);
	const TSharedRef<FJsonObject> SeedProperties = MakeShared<FJsonObject>();
	SeedProperties->SetStringField(TEXT("foo"), TEXT("bar"));
	SeedState->SetObjectField(TEXT("properties"), SeedProperties);
	Storage.SaveState(TEXT("super_properties"), SeedState);

	FPostHogSuperPropertiesManager Manager;

	AddExpectedError(TEXT("missing, malformed, or unsupported-version super property state"), EAutomationExpectedErrorFlags::Contains, 1);
	Manager.LoadOrCreate(Storage);

	TestEqual(TEXT("No super properties loaded from an unsupported schema version"), Manager.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogSuperPropertiesManagerUnregisterTest, "UnrealHog.SuperProperties.SuperPropertiesManager.UnregisterRemovesOnlyTargetedKey", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogSuperPropertiesManagerUnregisterTest::RunTest(const FString& Parameters)
{
	FPostHogInMemoryStorageProvider Storage;
	FPostHogSuperPropertiesManager Manager;
	Manager.LoadOrCreate(Storage);

	FPostHogEventProperty ValueA;
	ValueA.Type = EPostHogPropertyType::String;
	ValueA.StringValue = TEXT("a");
	Manager.Register(TEXT("key_a"), ValueA, Storage);

	FPostHogEventProperty ValueB;
	ValueB.Type = EPostHogPropertyType::String;
	ValueB.StringValue = TEXT("b");
	Manager.Register(TEXT("key_b"), ValueB, Storage);

	Manager.Unregister(TEXT("key_a"), Storage);

	TestEqual(TEXT("Only key_b remains"), Manager.Num(), 1);

	FPostHogSuperPropertiesManager ReloadedManager;
	ReloadedManager.LoadOrCreate(Storage);
	TestEqual(TEXT("Removal was persisted"), ReloadedManager.Num(), 1);

	// Unregistering a key that was never registered is a safe no-op.
	Manager.Unregister(TEXT("missing_key"), Storage);
	TestEqual(TEXT("Unregistering a missing key does not change count"), Manager.Num(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogSuperPropertiesManagerClearTest, "UnrealHog.SuperProperties.SuperPropertiesManager.ClearEmptiesAllKeysAndPersists", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogSuperPropertiesManagerClearTest::RunTest(const FString& Parameters)
{
	FPostHogInMemoryStorageProvider Storage;
	FPostHogSuperPropertiesManager Manager;
	Manager.LoadOrCreate(Storage);

	FPostHogEventProperty Value;
	Value.Type = EPostHogPropertyType::String;
	Value.StringValue = TEXT("a");
	Manager.Register(TEXT("key_a"), Value, Storage);

	Manager.Clear(Storage);

	TestEqual(TEXT("Manager has no properties after Clear"), Manager.Num(), 0);

	FPostHogSuperPropertiesManager ReloadedManager;
	ReloadedManager.LoadOrCreate(Storage);
	TestEqual(TEXT("Cleared (empty) set was persisted"), ReloadedManager.Num(), 0);

	FString StateJson;
	TestTrue(TEXT("State still present after Clear"), Storage.LoadState(TEXT("super_properties"), StateJson));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogSuperPropertiesManagerApplyToRichValuesTest, "UnrealHog.SuperProperties.SuperPropertiesManager.ApplyToSetsAllRichValueTypes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogSuperPropertiesManagerApplyToRichValuesTest::RunTest(const FString& Parameters)
{
	FPostHogInMemoryStorageProvider Storage;
	FPostHogSuperPropertiesManager Manager;
	Manager.LoadOrCreate(Storage);

	FPostHogEventProperty StringProp;
	StringProp.Type = EPostHogPropertyType::String;
	StringProp.StringValue = TEXT("string_value");
	Manager.Register(TEXT("string_key"), StringProp, Storage);

	FPostHogEventProperty NumberProp;
	NumberProp.Type = EPostHogPropertyType::Number;
	NumberProp.NumberValue = 42.5;
	Manager.Register(TEXT("number_key"), NumberProp, Storage);

	FPostHogEventProperty BoolProp;
	BoolProp.Type = EPostHogPropertyType::Boolean;
	BoolProp.bBoolValue = true;
	Manager.Register(TEXT("bool_key"), BoolProp, Storage);

	FPostHogEventProperty NullProp;
	NullProp.Type = EPostHogPropertyType::Null;
	Manager.Register(TEXT("null_key"), NullProp, Storage);

	FPostHogEventProperty ObjectProp;
	ObjectProp.Type = EPostHogPropertyType::Object;
	FPostHogEventProperty ObjectChild;
	ObjectChild.Key = TEXT("nested_key");
	ObjectChild.Type = EPostHogPropertyType::String;
	ObjectChild.StringValue = TEXT("nested_value");
	ObjectProp.Children.Add(ObjectChild);
	Manager.Register(TEXT("object_key"), ObjectProp, Storage);

	FPostHogEventProperty ArrayProp;
	ArrayProp.Type = EPostHogPropertyType::Array;
	FPostHogEventProperty ArrayElement;
	ArrayElement.Type = EPostHogPropertyType::Number;
	ArrayElement.NumberValue = 7.0;
	ArrayProp.Children.Add(ArrayElement);
	Manager.Register(TEXT("array_key"), ArrayProp, Storage);

	FPostHogEvent Event(TEXT("test-event"), TEXT("distinct-1"));
	Manager.ApplyTo(Event);

	const TSharedRef<FJsonObject> JsonObject = Event.ToJsonObject();
	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("JSON has properties object"), JsonObject->TryGetObjectField(TEXT("properties"), PropertiesObject));

	FString StringValue;
	TestTrue(TEXT("string_key exists"), (*PropertiesObject)->TryGetStringField(TEXT("string_key"), StringValue));
	TestEqual(TEXT("string_key value"), StringValue, FString(TEXT("string_value")));

	double NumberValue = 0.0;
	TestTrue(TEXT("number_key exists"), (*PropertiesObject)->TryGetNumberField(TEXT("number_key"), NumberValue));
	TestEqual(TEXT("number_key value"), NumberValue, 42.5);

	bool BoolValue = false;
	TestTrue(TEXT("bool_key exists"), (*PropertiesObject)->TryGetBoolField(TEXT("bool_key"), BoolValue));
	TestTrue(TEXT("bool_key value"), BoolValue);

	const TSharedPtr<FJsonValue> NullField = (*PropertiesObject)->TryGetField(TEXT("null_key"));
	TestTrue(TEXT("null_key exists"), NullField.IsValid());
	if (NullField.IsValid())
	{
		TestTrue(TEXT("null_key type is Null"), NullField->Type == EJson::Null);
	}

	const TSharedPtr<FJsonObject>* ObjectField = nullptr;
	TestTrue(TEXT("object_key exists and is an object"), (*PropertiesObject)->TryGetObjectField(TEXT("object_key"), ObjectField));
	if (ObjectField)
	{
		FString NestedValue;
		TestTrue(TEXT("nested_key exists"), (*ObjectField)->TryGetStringField(TEXT("nested_key"), NestedValue));
		TestEqual(TEXT("nested_key value"), NestedValue, FString(TEXT("nested_value")));
	}

	const TArray<TSharedPtr<FJsonValue>>* ArrayField = nullptr;
	TestTrue(TEXT("array_key exists and is an array"), (*PropertiesObject)->TryGetArrayField(TEXT("array_key"), ArrayField));
	if (ArrayField)
	{
		TestEqual(TEXT("array_key has one element"), ArrayField->Num(), 1);
		if (ArrayField->Num() == 1)
		{
			TestEqual(TEXT("array_key element value"), (*ArrayField)[0]->AsNumber(), 7.0);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogSuperPropertiesManagerSourceIndependenceTest, "UnrealHog.SuperProperties.SuperPropertiesManager.RegisterFromUObjectSourceIsIndependentOfLaterMutation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogSuperPropertiesManagerSourceIndependenceTest::RunTest(const FString& Parameters)
{
	FPostHogInMemoryStorageProvider Storage;
	FPostHogSuperPropertiesManager Manager;
	Manager.LoadOrCreate(Storage);

	{
		UPostHogEventProperties* SourceObject = NewObject<UPostHogEventProperties>();
		SourceObject->AddString(TEXT("child_key"), TEXT("original_value"));

		UPostHogEventPropertyArray* SourceArray = NewObject<UPostHogEventPropertyArray>();
		SourceArray->AddString(TEXT("original_element"));

		FPostHogEventProperty ObjectProperty;
		ObjectProperty.Type = EPostHogPropertyType::Object;
		ObjectProperty.Children = SourceObject->GetProperties();
		Manager.Register(TEXT("object_key"), ObjectProperty, Storage);

		FPostHogEventProperty ArrayProperty;
		ArrayProperty.Type = EPostHogPropertyType::Array;
		ArrayProperty.Children = SourceArray->GetElements();
		Manager.Register(TEXT("array_key"), ArrayProperty, Storage);

		// Mutate the source builders after registration; the stored value must not change.
		SourceObject->AddString(TEXT("child_key"), TEXT("mutated_value"));
		SourceArray->AddString(TEXT("mutated_element"));

		// Destroy the source builders; the manager holds an independent value copy.
		SourceObject->MarkAsGarbage();
		SourceArray->MarkAsGarbage();
	}

	FPostHogEvent Event(TEXT("test-event"), TEXT("distinct-1"));
	Manager.ApplyTo(Event);

	const TSharedRef<FJsonObject> JsonObject = Event.ToJsonObject();
	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("JSON has properties object"), JsonObject->TryGetObjectField(TEXT("properties"), PropertiesObject));

	const TSharedPtr<FJsonObject>* ObjectField = nullptr;
	TestTrue(TEXT("object_key exists and is an object"), (*PropertiesObject)->TryGetObjectField(TEXT("object_key"), ObjectField));
	if (ObjectField)
	{
		FString ChildValue;
		TestTrue(TEXT("child_key exists"), (*ObjectField)->TryGetStringField(TEXT("child_key"), ChildValue));
		TestEqual(TEXT("Registered object value is unaffected by later source mutation"), ChildValue, TEXT("original_value"));
	}

	const TArray<TSharedPtr<FJsonValue>>* ArrayField = nullptr;
	TestTrue(TEXT("array_key exists and is an array"), (*PropertiesObject)->TryGetArrayField(TEXT("array_key"), ArrayField));
	if (ArrayField)
	{
		TestEqual(TEXT("Registered array retains only the original element"), ArrayField->Num(), 1);
		if (ArrayField->Num() == 1)
		{
			TestEqual(TEXT("Registered array element is unaffected by later source mutation"), (*ArrayField)[0]->AsString(), TEXT("original_element"));
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
