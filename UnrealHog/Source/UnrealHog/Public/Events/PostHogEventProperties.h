// Trevor Eckhoff, 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "PostHogEventProperties.generated.h"

struct FPostHogEvent;

UENUM(BlueprintType)
enum class EPostHogPropertyType : uint8
{
	String,
	Number,
	Boolean,
	Null,
	Object,
	Array
};

USTRUCT(BlueprintType)
struct FPostHogEventProperty
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Key;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EPostHogPropertyType Type = EPostHogPropertyType::String;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "Type == EPostHogPropertyType::String", EditConditionHides))
	FString StringValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "Type == EPostHogPropertyType::Number", EditConditionHides))
	double NumberValue = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "Type == EPostHogPropertyType::Boolean", EditConditionHides))
	bool bBoolValue = false;

	// For Object type, each Child carries a Key (map entry). For Array type, Child.Key is ignored (ordered list).
	// Not a UPROPERTY: UHT does not support recursive USTRUCT-in-array reflection ("Struct recursion via arrays
	// is unsupported for properties"). Nested values are built exclusively via AddObject/AddArray/AddNull, so
	// editor/Blueprint field exposure of Children is unnecessary; the compiler-generated copy still deep-copies it.
	TArray<FPostHogEventProperty> Children;

};

class UPostHogEventProperties;

/**
 * Blueprint-safe builder for an ordered list of JSON values (used as the value of an Array property).
 */
UCLASS(BlueprintType)
class UNREALHOG_API UPostHogEventPropertyArray : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "PostHog|Events")
	UPostHogEventPropertyArray* AddString(const FString& StringValue);

	UFUNCTION(BlueprintCallable, Category = "PostHog|Events")
	UPostHogEventPropertyArray* AddNumber(double NumberValue);

	UFUNCTION(BlueprintCallable, Category = "PostHog|Events")
	UPostHogEventPropertyArray* AddBoolean(bool bBoolValue);

	UFUNCTION(BlueprintCallable, Category = "PostHog|Events")
	UPostHogEventPropertyArray* AddNull();

	UFUNCTION(BlueprintCallable, Category = "PostHog|Events")
	UPostHogEventPropertyArray* AddObject(UPostHogEventProperties* Value);

	UFUNCTION(BlueprintCallable, Category = "PostHog|Events")
	UPostHogEventPropertyArray* AddArray(UPostHogEventPropertyArray* Value);

	const TArray<FPostHogEventProperty>& GetElements() const { return Elements; }

private:
	TArray<FPostHogEventProperty> Elements;

};

/**
 *
 */
UCLASS(BlueprintType)
class UNREALHOG_API UPostHogEventProperties: public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "PostHog|Events")
	UPostHogEventProperties* AddString(const FString& Key, const FString& StringValue);

	UFUNCTION(BlueprintCallable, Category = "PostHog|Events")
	UPostHogEventProperties* AddNumber(const FString& Key, double NumberValue);

	UFUNCTION(BlueprintCallable, Category = "PostHog|Events")
	UPostHogEventProperties* AddBoolean(const FString& Key, bool bBoolValue);

	UFUNCTION(BlueprintCallable, Category = "PostHog|Events")
	UPostHogEventProperties* AddNull(const FString& Key);

	UFUNCTION(BlueprintCallable, Category = "PostHog|Events")
	UPostHogEventProperties* AddObject(const FString& Key, UPostHogEventProperties* Value);

	UFUNCTION(BlueprintCallable, Category = "PostHog|Events")
	UPostHogEventProperties* AddArray(const FString& Key, UPostHogEventPropertyArray* Value);

	void ApplyToEvent(FPostHogEvent& Event);

	const TArray<FPostHogEventProperty>& GetProperties() const { return Properties; }

private:
	TArray<FPostHogEventProperty> Properties;

};
