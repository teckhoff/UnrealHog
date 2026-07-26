#include "FeatureFlags/Models/PostHogFeatureFlag.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

TOptional<FPostHogFeatureFlagReason> FPostHogFeatureFlagReason::FromJson(const TSharedPtr<FJsonObject>& Object)
{
	if (!Object.IsValid())
	{
		return TOptional<FPostHogFeatureFlagReason>();
	}

	FPostHogFeatureFlagReason Reason;
	// description tolerated as optional/mistyped: only read it when present as a string.
	Object->TryGetStringField(TEXT("description"), Reason.Description);
	return Reason;
}

TSharedRef<FJsonObject> FPostHogFeatureFlagReason::ToJson() const
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("description"), Description);
	return Object;
}

TOptional<FPostHogFeatureFlagMetadata> FPostHogFeatureFlagMetadata::FromJson(const TSharedPtr<FJsonObject>& Object)
{
	if (!Object.IsValid())
	{
		return TOptional<FPostHogFeatureFlagMetadata>();
	}

	FPostHogFeatureFlagMetadata Metadata;

	// id/version parsed as numbers (JSON numbers are doubles) and truncated to int32, matching the
	// Unity long/int/double handling. Absent/mistyped fields keep the default 0.
	double IdValue = 0.0;
	if (Object->TryGetNumberField(TEXT("id"), IdValue))
	{
		Metadata.Id = static_cast<int32>(IdValue);
	}

	double VersionValue = 0.0;
	if (Object->TryGetNumberField(TEXT("version"), VersionValue))
	{
		Metadata.Version = static_cast<int32>(VersionValue);
	}

	// Payload preserved verbatim (any JSON type) so nested structures round-trip losslessly.
	if (Object->Values.Contains(TEXT("payload")))
	{
		Metadata.Payload = Object->TryGetField(TEXT("payload"));
	}

	return Metadata;
}

TSharedRef<FJsonObject> FPostHogFeatureFlagMetadata::ToJson() const
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetNumberField(TEXT("id"), Id);
	Object->SetNumberField(TEXT("version"), Version);

	if (Payload.IsValid())
	{
		Object->SetField(TEXT("payload"), Payload);
	}

	return Object;
}

FPostHogFlagValue FPostHogFeatureFlag::GetValue() const
{
	// Variant precedence: a non-empty variant wins over the boolean enabled state.
	if (!Variant.IsEmpty())
	{
		return FPostHogFlagValue::FromString(Variant);
	}

	return FPostHogFlagValue::FromBool(Enabled.Get(false));
}

TSharedPtr<FJsonValue> FPostHogFeatureFlag::GetPayload() const
{
	if (Metadata.IsSet())
	{
		return Metadata.GetValue().Payload;
	}

	return nullptr;
}

TOptional<FPostHogFeatureFlag> FPostHogFeatureFlag::FromJson(const TSharedPtr<FJsonObject>& Object)
{
	if (!Object.IsValid())
	{
		return TOptional<FPostHogFeatureFlag>();
	}

	FPostHogFeatureFlag Flag;

	// enabled only adopted when it is actually a boolean, mirroring Unity's `enabled is bool b`.
	bool bEnabled = false;
	if (Object->TryGetBoolField(TEXT("enabled"), bEnabled))
	{
		Flag.Enabled = bEnabled;
	}

	// variant only adopted when it is a string.
	Object->TryGetStringField(TEXT("variant"), Flag.Variant);

	const TSharedPtr<FJsonObject>* MetadataObject = nullptr;
	if (Object->TryGetObjectField(TEXT("metadata"), MetadataObject))
	{
		Flag.Metadata = FPostHogFeatureFlagMetadata::FromJson(*MetadataObject);
	}

	const TSharedPtr<FJsonObject>* ReasonObject = nullptr;
	if (Object->TryGetObjectField(TEXT("reason"), ReasonObject))
	{
		Flag.Reason = FPostHogFeatureFlagReason::FromJson(*ReasonObject);
	}

	return Flag;
}

TSharedRef<FJsonObject> FPostHogFeatureFlag::ToJson() const
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();

	if (Enabled.IsSet())
	{
		Object->SetBoolField(TEXT("enabled"), Enabled.GetValue());
	}

	if (!Variant.IsEmpty())
	{
		Object->SetStringField(TEXT("variant"), Variant);
	}

	if (Metadata.IsSet())
	{
		Object->SetObjectField(TEXT("metadata"), Metadata.GetValue().ToJson());
	}

	if (Reason.IsSet())
	{
		Object->SetObjectField(TEXT("reason"), Reason.GetValue().ToJson());
	}

	return Object;
}
