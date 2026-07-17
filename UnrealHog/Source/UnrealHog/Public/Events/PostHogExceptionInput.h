#pragma once

#include "CoreMinimal.h"

#include "PostHogExceptionInput.generated.h"

/**
 * @brief Caller-supplied description of a single exception, passed to
 * UPostHogRuntimeSubsystem::CaptureException / FPostHogConsentController::CaptureException.
 *
 * StackTrace is raw, newline-delimited text rather than a structured frame list: this SDK does
 * not parse or invent cross-language stack frame fields (see EP-014 exclusions).
 */
USTRUCT(BlueprintType)
struct FPostHogExceptionInput
{
	GENERATED_BODY()

	// Required. The exception message ($exception_message / exception_list[0].value). Blank or
	// whitespace-only is treated as invalid input by CaptureException.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PostHog|Exceptions")
	FString Message;

	// Required. The exception type ($exception_type / exception_list[0].type). Blank or
	// whitespace-only is treated as invalid input by CaptureException.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PostHog|Exceptions")
	FString Type;

	// Optional raw stack trace text, split on newlines into exception_list[0].stacktrace.frames.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PostHog|Exceptions")
	FString StackTrace;

	// Whether this exception was handled by application code ($exception_handled /
	// exception_list[0].mechanism.handled). Defaults to true (manual capture is inherently a
	// handled report).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PostHog|Exceptions")
	bool bHandled = true;
};
