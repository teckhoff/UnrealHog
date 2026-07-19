#include "FeatureFlags/Models/PostHogFeatureFlagsResponse.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Logging/PostHogLogger.h"

TOptional<FPostHogFeatureFlagsResponse> FPostHogFeatureFlagsResponse::FromJson(const TSharedPtr<FJsonObject>& Object)
{
	// The only hard-fail: an invalid root object. Everything below tolerates missing/mistyped fields
	// so a valid cache is never replaced by partially-malformed input.
	if (!Object.IsValid())
	{
		return TOptional<FPostHogFeatureFlagsResponse>();
	}

	// Unversioned data is treated as v1; a newer-than-supported version is warned about but parsed.
	int32 Version = 1;
	double VersionValue = 0.0;
	if (Object->TryGetNumberField(TEXT("_version"), VersionValue))
	{
		Version = static_cast<int32>(VersionValue);
	}

	if (Version > CurrentVersion)
	{
		UE_LOG(LogUnrealHog, Warning,
			TEXT("Feature flags cache version %d is newer than supported version %d, data may be incompatible"),
			Version, CurrentVersion);
	}

	FPostHogFeatureFlagsResponse Response;

	Object->TryGetBoolField(TEXT("errorsWhileComputingFlags"), Response.bErrorsWhileComputingFlags);

	const TSharedPtr<FJsonObject>* FeatureFlagsObject = nullptr;
	if (Object->TryGetObjectField(TEXT("featureFlags"), FeatureFlagsObject))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*FeatureFlagsObject)->Values)
		{
			Response.FeatureFlags.Add(Pair.Key, Pair.Value);
		}
	}

	const TSharedPtr<FJsonObject>* PayloadsObject = nullptr;
	if (Object->TryGetObjectField(TEXT("featureFlagPayloads"), PayloadsObject))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*PayloadsObject)->Values)
		{
			Response.FeatureFlagPayloads.Add(Pair.Key, Pair.Value);
		}
	}

	const TSharedPtr<FJsonObject>* FlagsObject = nullptr;
	if (Object->TryGetObjectField(TEXT("flags"), FlagsObject))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*FlagsObject)->Values)
		{
			// Skip non-object flag entries, matching Unity's `kvp.Value is Dictionary` guard.
			if (!Pair.Value.IsValid() || Pair.Value->Type != EJson::Object)
			{
				continue;
			}

			TOptional<FPostHogFeatureFlag> Flag = FPostHogFeatureFlag::FromJson(Pair.Value->AsObject());
			if (Flag.IsSet())
			{
				Response.Flags.Add(Pair.Key, Flag.GetValue());
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* QuotaLimitedArray = nullptr;
	if (Object->TryGetArrayField(TEXT("quotaLimited"), QuotaLimitedArray))
	{
		for (const TSharedPtr<FJsonValue>& Item : *QuotaLimitedArray)
		{
			if (!Item.IsValid())
			{
				continue;
			}

			FString KeyValue;
			if (Item->TryGetString(KeyValue))
			{
				Response.QuotaLimited.Add(KeyValue);
			}
		}
	}

	FString RequestId;
	if (Object->TryGetStringField(TEXT("requestId"), RequestId))
	{
		Response.RequestId = RequestId;
	}

	double EvaluatedAtValue = 0.0;
	if (Object->TryGetNumberField(TEXT("evaluatedAt"), EvaluatedAtValue))
	{
		Response.EvaluatedAt = static_cast<int64>(EvaluatedAtValue);
	}

	return Response;
}

TSharedRef<FJsonObject> FPostHogFeatureFlagsResponse::ToJson() const
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();

	// Always stamp the schema version and the errors flag so the cache shape is stable.
	Object->SetNumberField(TEXT("_version"), CurrentVersion);
	Object->SetBoolField(TEXT("errorsWhileComputingFlags"), bErrorsWhileComputingFlags);

	if (FeatureFlags.Num() > 0)
	{
		TSharedRef<FJsonObject> FeatureFlagsObject = MakeShared<FJsonObject>();
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : FeatureFlags)
		{
			FeatureFlagsObject->SetField(Pair.Key, Pair.Value);
		}
		Object->SetObjectField(TEXT("featureFlags"), FeatureFlagsObject);
	}

	if (FeatureFlagPayloads.Num() > 0)
	{
		TSharedRef<FJsonObject> PayloadsObject = MakeShared<FJsonObject>();
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : FeatureFlagPayloads)
		{
			PayloadsObject->SetField(Pair.Key, Pair.Value);
		}
		Object->SetObjectField(TEXT("featureFlagPayloads"), PayloadsObject);
	}

	if (Flags.Num() > 0)
	{
		TSharedRef<FJsonObject> FlagsObject = MakeShared<FJsonObject>();
		for (const TPair<FString, FPostHogFeatureFlag>& Pair : Flags)
		{
			FlagsObject->SetObjectField(Pair.Key, Pair.Value.ToJson());
		}
		Object->SetObjectField(TEXT("flags"), FlagsObject);
	}

	if (QuotaLimited.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> QuotaLimitedArray;
		for (const FString& Key : QuotaLimited)
		{
			QuotaLimitedArray.Add(MakeShared<FJsonValueString>(Key));
		}
		Object->SetArrayField(TEXT("quotaLimited"), QuotaLimitedArray);
	}

	if (RequestId.IsSet())
	{
		Object->SetStringField(TEXT("requestId"), RequestId.GetValue());
	}

	if (EvaluatedAt.IsSet())
	{
		Object->SetNumberField(TEXT("evaluatedAt"), static_cast<double>(EvaluatedAt.GetValue()));
	}

	return Object;
}

FPostHogFlagValue FPostHogFeatureFlagsResponse::ResolveValue(const FString& Key) const
{
	// v4 precedence: a v4 flag entry wins over the legacy value for the same key.
	if (const FPostHogFeatureFlag* Flag = Flags.Find(Key))
	{
		return Flag->GetValue();
	}

	if (const TSharedPtr<FJsonValue>* LegacyValue = FeatureFlags.Find(Key))
	{
		return FPostHogFlagValue::FromJson(*LegacyValue);
	}

	return FPostHogFlagValue::Missing();
}

TSharedPtr<FJsonValue> FPostHogFeatureFlagsResponse::ResolvePayload(const FString& Key) const
{
	// v4 precedence: a v4 metadata payload wins over the legacy payload for the same key.
	if (const FPostHogFeatureFlag* Flag = Flags.Find(Key))
	{
		if (TSharedPtr<FJsonValue> Payload = Flag->GetPayload())
		{
			return Payload;
		}
	}

	if (const TSharedPtr<FJsonValue>* LegacyPayload = FeatureFlagPayloads.Find(Key))
	{
		return *LegacyPayload;
	}

	return nullptr;
}
