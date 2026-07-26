#pragma once

#include "CoreMinimal.h"
#include "PostHogFlagValue.generated.h"

class FJsonValue;

/**
 * @brief Distinguishes the three observable states a resolved feature-flag value can take.
 *
 * Mirrors the Unity FlagValue struct (FlagValue in PostHogFeatureFlag.cs): a flag can be missing
 * entirely, a Boolean enabled/disabled state, or a string variant. String variants are still a
 * String value even when the variant text is empty, matching Unity where an empty variant "has a
 * value" but is not "enabled".
 */
UENUM(BlueprintType)
enum class EPostHogFlagValueType : uint8
{
	/** No value exists for the key. */
	Missing,

	/** A boolean enabled/disabled flag. */
	Boolean,

	/** A multivariate string variant. */
	String
};

/**
 * @brief Blueprint-friendly resolved value of a feature flag.
 *
 * This is the only public, reflected type in the feature-flag model set; the private v3/v4 response
 * models stay out of the reflected API surface. It intentionally does not expose raw JSON: callers
 * see a discriminated Missing/Boolean/String value that mirrors Unity's FlagValue semantics.
 */
USTRUCT(BlueprintType)
struct UNREALHOG_API FPostHogFlagValue
{
	GENERATED_BODY()

	/** Which of the three states this value represents. */
	UPROPERTY(BlueprintReadOnly, Category = "PostHog|FeatureFlags")
	EPostHogFlagValueType Type = EPostHogFlagValueType::Missing;

	/** Boolean payload; only meaningful when Type == Boolean. */
	UPROPERTY(BlueprintReadOnly, Category = "PostHog|FeatureFlags")
	bool bBoolValue = false;

	/** String variant payload; only meaningful when Type == String. */
	UPROPERTY(BlueprintReadOnly, Category = "PostHog|FeatureFlags")
	FString StringValue;

	/** Constructs a Missing value (no flag set for the key). */
	static FPostHogFlagValue Missing();

	/** Constructs a Boolean value. */
	static FPostHogFlagValue FromBool(bool bValue);

	/**
	 * Constructs a String variant value. The result is always Type == String even for an empty
	 * string: it HasValue but IsEnabled returns false, matching Unity's empty-variant behavior.
	 */
	static FPostHogFlagValue FromString(const FString& InValue);

	/**
	 * Builds a value from a raw JSON value: booleans become Boolean, strings become String, and
	 * anything else (including null / unset) becomes Missing.
	 */
	static FPostHogFlagValue FromJson(const TSharedPtr<FJsonValue>& JsonValue);

	/** Whether a value exists (Boolean or String). */
	bool HasValue() const { return Type != EPostHogFlagValueType::Missing; }

	/** Whether this is a Boolean value. */
	bool IsBool() const { return Type == EPostHogFlagValueType::Boolean; }

	/** Whether this is a String variant value. */
	bool IsString() const { return Type == EPostHogFlagValueType::String; }

	/**
	 * Whether the flag is "enabled": true for a Boolean true, non-empty for a String variant, and
	 * false when Missing or an empty variant.
	 */
	bool IsEnabled() const;

	/** Human-readable representation: "null" when Missing, the bool, or the variant string. */
	FString ToDisplayString() const;
};
