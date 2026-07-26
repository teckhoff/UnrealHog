#include "FeatureFlags/PostHogFlagValue.h"

#include "Dom/JsonValue.h"

FPostHogFlagValue FPostHogFlagValue::Missing()
{
	FPostHogFlagValue Value;
	Value.Type = EPostHogFlagValueType::Missing;
	return Value;
}

FPostHogFlagValue FPostHogFlagValue::FromBool(bool bValue)
{
	FPostHogFlagValue Value;
	Value.Type = EPostHogFlagValueType::Boolean;
	Value.bBoolValue = bValue;
	return Value;
}

FPostHogFlagValue FPostHogFlagValue::FromString(const FString& InValue)
{
	// Type is String even for an empty variant: it HasValue but is not IsEnabled, matching Unity's
	// FlagValue(string) where _hasValue is true for a non-null (including empty) string.
	FPostHogFlagValue Value;
	Value.Type = EPostHogFlagValueType::String;
	Value.StringValue = InValue;
	return Value;
}

FPostHogFlagValue FPostHogFlagValue::FromJson(const TSharedPtr<FJsonValue>& JsonValue)
{
	if (!JsonValue.IsValid())
	{
		return Missing();
	}

	switch (JsonValue->Type)
	{
	case EJson::Boolean:
		return FromBool(JsonValue->AsBool());
	case EJson::String:
		return FromString(JsonValue->AsString());
	default:
		return Missing();
	}
}

bool FPostHogFlagValue::IsEnabled() const
{
	switch (Type)
	{
	case EPostHogFlagValueType::Boolean:
		return bBoolValue;
	case EPostHogFlagValueType::String:
		return !StringValue.IsEmpty();
	default:
		return false;
	}
}

FString FPostHogFlagValue::ToDisplayString() const
{
	switch (Type)
	{
	case EPostHogFlagValueType::Boolean:
		return bBoolValue ? TEXT("true") : TEXT("false");
	case EPostHogFlagValueType::String:
		return StringValue;
	default:
		return TEXT("null");
	}
}
