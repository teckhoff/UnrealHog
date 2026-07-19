#pragma once

#include "CoreMinimal.h"
#include "PostHogLogger.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogUnrealHog, Log, All);

UENUM(BlueprintType)
enum class EPostHogLogLevel : uint8
{
	Debug,
	Info,
	Warning,
	Error,
	None
};

class PostHogLogger
{
public:
	PostHogLogger() = delete;
};
