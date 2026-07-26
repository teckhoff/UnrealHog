#pragma once

#include "CoreMinimal.h"
#include "FeatureFlags/PostHogFlagValue.h"
#include "Misc/Optional.h"

class FJsonObject;
class FJsonValue;

/**
 * @brief Private v4 feature-flag models mirroring the Unity FeatureFlag/FeatureFlagMetadata/
 * FeatureFlagReason classes (FeatureFlag.cs).
 *
 * These are deliberately plain C++ structs (not USTRUCT): they are implementation-only response and
 * cache shapes and must stay out of the reflected/Blueprint API surface per the SDKP-006 exclusions.
 * Only FPostHogFlagValue is public. Parsing tolerates missing/mistyped optional fields and returns
 * an unset TOptional only when the required object shape itself is absent, so a valid cache is never
 * clobbered by malformed input.
 */

/** The reason a flag resolved to its value. Serializes losslessly for caching. */
struct FPostHogFeatureFlagReason
{
	FString Description;

	/** Parses from a "reason" object; unset when the object is invalid. */
	static TOptional<FPostHogFeatureFlagReason> FromJson(const TSharedPtr<FJsonObject>& Object);

	/** Serializes back to a "reason" object. */
	TSharedRef<FJsonObject> ToJson() const;
};

/** Flag metadata (id, version, optional payload). Serializes losslessly for caching. */
struct FPostHogFeatureFlagMetadata
{
	int32 Id = 0;
	int32 Version = 0;

	/** Raw payload JSON preserved verbatim so nested structures survive round-tripping. */
	TSharedPtr<FJsonValue> Payload;

	/** Parses from a "metadata" object; unset when the object is invalid. */
	static TOptional<FPostHogFeatureFlagMetadata> FromJson(const TSharedPtr<FJsonObject>& Object);

	/** Serializes back to a "metadata" object: always id+version, payload only when present. */
	TSharedRef<FJsonObject> ToJson() const;
};

/** A single v4 flag entry. */
struct FPostHogFeatureFlag
{
	/** Boolean enabled state, unset when the response omitted it. */
	TOptional<bool> Enabled;

	/** Variant name; empty means no variant. */
	FString Variant;

	TOptional<FPostHogFeatureFlagMetadata> Metadata;
	TOptional<FPostHogFeatureFlagReason> Reason;

	/**
	 * Resolves the value with variant-over-enabled precedence: a non-empty variant becomes a String
	 * value, otherwise the Boolean enabled state (defaulting to false when unset). Mirrors Unity
	 * FeatureFlag.GetValue.
	 */
	FPostHogFlagValue GetValue() const;

	/** The metadata payload, or null when there is no metadata/payload. */
	TSharedPtr<FJsonValue> GetPayload() const;

	/** Parses from a flag object; unset when the object is invalid. */
	static TOptional<FPostHogFeatureFlag> FromJson(const TSharedPtr<FJsonObject>& Object);

	/** Serializes back to a flag object, omitting absent optional fields. */
	TSharedRef<FJsonObject> ToJson() const;
};
