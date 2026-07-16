
#include "Events/PostHogEventProperties.h"
#include "Events/PostHogCapturePolicy.h"
#include "Events/PostHogEvent.h"
#include "Events/PostHogPropertyJson.h"
#include "Dom/JsonValue.h"
#include "Logging/PostHogLogger.h"

UPostHogEventProperties* UPostHogEventProperties::AddString(const FString& Key, const FString& StringValue)
{
	FPostHogEventProperty NewStringProperty;
	NewStringProperty.Key = Key;
	NewStringProperty.Type = EPostHogPropertyType::String;
	NewStringProperty.StringValue = StringValue;
	
	Properties.Add(NewStringProperty);
	
	return this;
}

UPostHogEventProperties* UPostHogEventProperties::AddNumber(const FString& Key, double NumberValue)
{
	FPostHogEventProperty NewNumberProperty;
	NewNumberProperty.Key = Key;
	NewNumberProperty.Type = EPostHogPropertyType::Number;
	NewNumberProperty.NumberValue = NumberValue;
	
	Properties.Add(NewNumberProperty);
	
	return this;
}

UPostHogEventProperties* UPostHogEventProperties::AddBoolean(const FString& Key, bool bBoolValue)
{
	FPostHogEventProperty NewBooleanProperty;
	NewBooleanProperty.Key = Key;
	NewBooleanProperty.Type = EPostHogPropertyType::Boolean;
	NewBooleanProperty.bBoolValue = bBoolValue;
	
	Properties.Add(NewBooleanProperty);
	
	return this;
}

UPostHogEventProperties* UPostHogEventProperties::AddNull(const FString& Key)
{
	FPostHogEventProperty NewNullProperty;
	NewNullProperty.Key = Key;
	NewNullProperty.Type = EPostHogPropertyType::Null;

	Properties.Add(NewNullProperty);

	return this;
}

UPostHogEventProperties* UPostHogEventProperties::AddObject(const FString& Key, UPostHogEventProperties* Value)
{
	FPostHogEventProperty NewObjectProperty;
	NewObjectProperty.Key = Key;
	NewObjectProperty.Type = EPostHogPropertyType::Object;

	if (Value)
	{
		NewObjectProperty.Children = Value->GetProperties();
	}

	Properties.Add(NewObjectProperty);

	return this;
}

UPostHogEventProperties* UPostHogEventProperties::AddArray(const FString& Key, UPostHogEventPropertyArray* Value)
{
	FPostHogEventProperty NewArrayProperty;
	NewArrayProperty.Key = Key;
	NewArrayProperty.Type = EPostHogPropertyType::Array;

	if (Value)
	{
		NewArrayProperty.Children = Value->GetElements();
	}

	Properties.Add(NewArrayProperty);

	return this;
}

void UPostHogEventProperties::ApplyToEvent(FPostHogEvent& Event)
{
	for (const auto& Property : Properties)
	{
		if (Property.Key.IsEmpty())
		{
			continue;
		}

		if (PostHogCapturePolicy::GetReservedPropertyKeys().Contains(Property.Key))
		{
#if !WITH_DEV_AUTOMATION_TESTS
			UE_LOG(LogPostHog, Warning, TEXT("Ignoring attempt to overwrite protected PostHog property \"%s\"; reserved properties are SDK-owned and cannot be overwritten."), *Property.Key);
#endif
			continue;
		}

		Event.SetJsonValueProperty(Property.Key, PostHogPropertyJson::ToJsonValue(Property));
	}
}

UPostHogEventPropertyArray* UPostHogEventPropertyArray::AddString(const FString& StringValue)
{
	FPostHogEventProperty NewStringProperty;
	NewStringProperty.Type = EPostHogPropertyType::String;
	NewStringProperty.StringValue = StringValue;

	Elements.Add(NewStringProperty);

	return this;
}

UPostHogEventPropertyArray* UPostHogEventPropertyArray::AddNumber(double NumberValue)
{
	FPostHogEventProperty NewNumberProperty;
	NewNumberProperty.Type = EPostHogPropertyType::Number;
	NewNumberProperty.NumberValue = NumberValue;

	Elements.Add(NewNumberProperty);

	return this;
}

UPostHogEventPropertyArray* UPostHogEventPropertyArray::AddBoolean(bool bBoolValue)
{
	FPostHogEventProperty NewBooleanProperty;
	NewBooleanProperty.Type = EPostHogPropertyType::Boolean;
	NewBooleanProperty.bBoolValue = bBoolValue;

	Elements.Add(NewBooleanProperty);

	return this;
}

UPostHogEventPropertyArray* UPostHogEventPropertyArray::AddNull()
{
	FPostHogEventProperty NewNullProperty;
	NewNullProperty.Type = EPostHogPropertyType::Null;

	Elements.Add(NewNullProperty);

	return this;
}

UPostHogEventPropertyArray* UPostHogEventPropertyArray::AddObject(UPostHogEventProperties* Value)
{
	FPostHogEventProperty NewObjectProperty;
	NewObjectProperty.Type = EPostHogPropertyType::Object;

	if (Value)
	{
		NewObjectProperty.Children = Value->GetProperties();
	}

	Elements.Add(NewObjectProperty);

	return this;
}

UPostHogEventPropertyArray* UPostHogEventPropertyArray::AddArray(UPostHogEventPropertyArray* Value)
{
	FPostHogEventProperty NewArrayProperty;
	NewArrayProperty.Type = EPostHogPropertyType::Array;

	if (Value)
	{
		NewArrayProperty.Children = Value->GetElements();
	}

	Elements.Add(NewArrayProperty);

	return this;
}
