#include "Events/PostHogPropertyJson.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Events/PostHogEventProperties.h"

TSharedRef<FJsonValue> PostHogPropertyJson::ToJsonValue(const FPostHogEventProperty& Property)
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
				ObjectValue->SetField(Child.Key, ToJsonValue(Child));
			}
			return MakeShared<FJsonValueObject>(ObjectValue);
		}
		case EPostHogPropertyType::Array:
		{
			TArray<TSharedPtr<FJsonValue>> ArrayValue;
			ArrayValue.Reserve(Property.Children.Num());
			for (const FPostHogEventProperty& Child : Property.Children)
			{
				ArrayValue.Add(ToJsonValue(Child));
			}
			return MakeShared<FJsonValueArray>(ArrayValue);
		}
		case EPostHogPropertyType::Null:
		default:
			return MakeShared<FJsonValueNull>();
	}
}

FPostHogEventProperty PostHogPropertyJson::FromJsonValue(const FString& Key, const FJsonValue& Value)
{
	FPostHogEventProperty Property;
	Property.Key = Key;

	switch (Value.Type)
	{
		case EJson::String:
			Property.Type = EPostHogPropertyType::String;
			Property.StringValue = Value.AsString();
			break;
		case EJson::Number:
			Property.Type = EPostHogPropertyType::Number;
			Property.NumberValue = Value.AsNumber();
			break;
		case EJson::Boolean:
			Property.Type = EPostHogPropertyType::Boolean;
			Property.bBoolValue = Value.AsBool();
			break;
		case EJson::Object:
		{
			Property.Type = EPostHogPropertyType::Object;
			const TSharedPtr<FJsonObject> ObjectValue = Value.AsObject();
			if (ObjectValue.IsValid())
			{
				for (const auto& FieldPair : ObjectValue->Values)
				{
					if (FieldPair.Key.IsEmpty() || !FieldPair.Value.IsValid())
					{
						continue;
					}
					Property.Children.Add(FromJsonValue(FString(*FieldPair.Key), *FieldPair.Value));
				}
			}
			break;
		}
		case EJson::Array:
		{
			Property.Type = EPostHogPropertyType::Array;
			for (const TSharedPtr<FJsonValue>& Element : Value.AsArray())
			{
				if (!Element.IsValid())
				{
					continue;
				}
				Property.Children.Add(FromJsonValue(FString(), *Element));
			}
			break;
		}
		case EJson::Null:
		case EJson::None:
		default:
			Property.Type = EPostHogPropertyType::Null;
			break;
	}

	return Property;
}
