#pragma once

#include "CoreMinimal.h"
#include "FeatureFlags/Models/PostHogFeatureFlag.h"
#include "FeatureFlags/PostHogFlagValue.h"
#include "Misc/Optional.h"

class FJsonObject;
class FJsonValue;

/**
 * @brief Private model for the `/flags/?v=2` response and its cached representation, mirroring the
 * Unity FeatureFlagsResponse (FeatureFlagsResponse.cs).
 *
 * Holds both the legacy v3 maps (featureFlags / featureFlagPayloads) and the v4 flags map so mixed
 * responses parse deterministically. Serialization is lossless and stamps an explicit cache schema
 * version (_version) so future readers can detect incompatible caches. This type is
 * implementation-only and is never exposed to Blueprints; callers interact through FPostHogFlagValue
 * via ResolveValue/ResolvePayload.
 */
struct FPostHogFeatureFlagsResponse
{
	/**
	 * Current cache schema version, matching the /flags API version (v=2). Persisted as _version and
	 * used to warn when a newer cache is read back.
	 */
	static constexpr int32 CurrentVersion = 2;

	/** Whether the server reported errors while computing flags. */
	bool bErrorsWhileComputingFlags = false;

	/** Legacy v3 flag values (key -> raw bool/string JSON value). */
	TMap<FString, TSharedPtr<FJsonValue>> FeatureFlags;

	/** Legacy v3 payloads (key -> raw JSON payload). */
	TMap<FString, TSharedPtr<FJsonValue>> FeatureFlagPayloads;

	/** v4 flags with metadata (key -> flag). */
	TMap<FString, FPostHogFeatureFlag> Flags;

	/** Keys that were quota limited. */
	TArray<FString> QuotaLimited;

	/** Server-generated request id for correlation. */
	TOptional<FString> RequestId;

	/** Unix timestamp when flags were evaluated. */
	TOptional<int64> EvaluatedAt;

	/**
	 * Parses a response/cache object. Returns unset only when the root object itself is invalid
	 * (the sole hard-fail, so a valid cache is never replaced by malformed input); every field-level
	 * type mismatch or unknown field is tolerated and skipped. A _version newer than CurrentVersion
	 * is logged as a warning but still parsed.
	 */
	static TOptional<FPostHogFeatureFlagsResponse> FromJson(const TSharedPtr<FJsonObject>& Object);

	/** Serializes losslessly, always writing _version and errorsWhileComputingFlags. */
	TSharedRef<FJsonObject> ToJson() const;

	/**
	 * Resolves a key's value with v4 precedence: a v4 Flags entry wins over the legacy featureFlags
	 * value; legacy-only keys still resolve; unknown keys return Missing.
	 */
	FPostHogFlagValue ResolveValue(const FString& Key) const;

	/**
	 * Resolves a key's payload with v4 precedence: a v4 metadata payload wins over the legacy
	 * featureFlagPayloads entry; returns null when neither is present.
	 */
	TSharedPtr<FJsonValue> ResolvePayload(const FString& Key) const;
};
