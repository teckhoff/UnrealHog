// Trevor Eckhoff, 2026. All rights reserved.


#include "Events/PostHogEventProperties.h"
#include "Events/PostHogEvent.h"

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

void UPostHogEventProperties::ApplyToEvent(FPostHogEvent& Event)
{
	for (const auto& Property : Properties)
	{
		if (Property.Key.IsEmpty())
		{
			continue;
		}
		
		switch (Property.Type)
		{
			case EPostHogPropertyType::String:
				Event.SetStringProperty(Property.Key, Property.StringValue);
				break;
			case EPostHogPropertyType::Number:
				Event.SetNumberProperty(Property.Key, Property.NumberValue);
				break;
			case EPostHogPropertyType::Boolean:
				Event.SetBoolProperty(Property.Key, Property.bBoolValue);
				break;
		}
	}
}
