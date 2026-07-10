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
	Boolean
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
	
	void ApplyToEvent(FPostHogEvent& Event);
	
private:
	TArray<FPostHogEventProperty> Properties;

};
