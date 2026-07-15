
#include "Events/PostHogEventProperties.h"
#include "Events/PostHogEvent.h"
#include "Dom/JsonValue.h"

namespace
{
	TSharedRef<FJsonValue> ConvertPropertyToJsonValue(const FPostHogEventProperty& Property)
	{
		switch (Property.Type)
		{
			case EPostHogPropertyType::String:
				return MakeShared<FJsonValueString>(Property.StringValue);
			case EPostHogPropertyType::Number:
				return MakeShared<FJsonValueNumber>(Property.NumberValue);
			case EPostHogPropertyType::Boolean:
				return MakeShared<FJsonValueBoolean>(Property.bBoolValue);
			case EPostHogPropertyType::Object:
			{
				const TSharedRef<FJsonObject> ObjectValue = MakeShared<FJsonObject>();
				for (const FPostHogEventProperty& Child : Property.Children)
				{
					if (Child.Key.IsEmpty())
					{
						continue;
					}
					ObjectValue->SetField(Child.Key, ConvertPropertyToJsonValue(Child));
				}
				return MakeShared<FJsonValueObject>(ObjectValue);
			}
			case EPostHogPropertyType::Array:
			{
				TArray<TSharedPtr<FJsonValue>> ArrayValue;
				ArrayValue.Reserve(Property.Children.Num());
				for (const FPostHogEventProperty& Child : Property.Children)
				{
					ArrayValue.Add(ConvertPropertyToJsonValue(Child));
				}
				return MakeShared<FJsonValueArray>(ArrayValue);
			}
			case EPostHogPropertyType::Null:
			default:
				return MakeShared<FJsonValueNull>();
		}
	}
}

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

		Event.SetJsonValueProperty(Property.Key, ConvertPropertyToJsonValue(Property));
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
