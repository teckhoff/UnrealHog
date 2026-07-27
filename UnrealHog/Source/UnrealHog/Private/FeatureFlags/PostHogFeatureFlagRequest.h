#pragma once

#include "CoreMinimal.h"
#include "Misc/Optional.h"

class FJsonObject;
class FJsonValue;

/**
 * @brief Private request body for `POST /flags/?v=2`, mirroring Unity's CreateFlagsRequest
 * (NetworkClient.cs).
 *
 * Implementation-only: never exposed to Blueprints and never constructed before analytics
 * collection is permitted. Optional members are omitted from the serialized body when unset or
 * empty, exactly as the reference does, so absent evaluation context is never sent as an empty
 * object. Person and group property values are carried as raw FJsonValue so nested types survive
 * round-tripping without being flattened to strings.
 */
struct FPostHogFeatureFlagRequest
{
	/** Project public API key. Required. */
	FString ApiKey;

	/** Effective distinct id (identified id if known, else the anonymous id). Required. */
	FString DistinctId;

	/** Serialized as `$anon_distinct_id`; omitted when unset or blank. */
	FString AnonymousId;

	/** Serialized as `$groups` (group type -> group key); omitted when empty. */
	TMap<FString, FString> Groups;

	/** Serialized as `person_properties`; omitted when empty. */
	TMap<FString, TSharedPtr<FJsonValue>> PersonProperties;

	/** Serialized as `group_properties` (group type -> properties); omitted when empty. */
	TMap<FString, TMap<FString, TSharedPtr<FJsonValue>>> GroupProperties;

	/** Builds the request body, writing only required fields and non-empty optional fields. */
	TSharedRef<FJsonObject> ToJsonObject() const;

	/** Serializes ToJsonObject() into a compact JSON string. Returns false on writer failure. */
	bool ToJsonString(FString& OutJson) const;
};
