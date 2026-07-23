#pragma once

#include "CoreMinimal.h"
#include "Events/PostHogExceptionInput.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UObject/Stack.h"

#include "PostHogExceptionLibrary.generated.h"

/**
 * @brief Convenience helpers that build an FPostHogExceptionInput describing "what failed here",
 * capturing the call stack appropriate to the caller.
 *
 * These helpers only allocate an in-memory struct: they never touch UPostHogRuntimeSubsystem,
 * consent state, the event queue, storage, or HTTP. They are therefore safe to call before
 * analytics opt-in; only CaptureException can enqueue anything, and only when collection is
 * permitted.
 *
 * Blank message/type validation stays owned by CaptureException; these helpers copy their inputs
 * through unchanged.
 */
UCLASS()
class UNREALHOG_API UPostHogExceptionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Builds an exception input carrying the current call stack.
	 *
	 * C++ callers reach the native body below, which walks the native stack via FPlatformStackWalk.
	 * Blueprint callers reach execMakeExceptionWithCurrentStack, which reads the Blueprint VM stack
	 * from the active custom-thunk FFrame. Native stack capture is best-effort in Shipping: exact
	 * symbols, file names, line numbers, and even nonempty output are not guaranteed.
	 */
	UFUNCTION(BlueprintCallable, CustomThunk, Category = "PostHog|Exceptions", meta = (DisplayName = "Make Exception With Current Stack"))
	static FPostHogExceptionInput MakeExceptionWithCurrentStack(
		const FString& Message,
		const FString& Type,
		bool bHandled = true);

	/** Builds an exception input with a native stack trace from FPlatformStackWalk (best-effort). */
	static FPostHogExceptionInput MakeExceptionWithNativeStack(
		const FString& Message,
		const FString& Type,
		bool bHandled = true);

	/** Builds an exception input from caller-supplied stack text, copied verbatim. */
	static FPostHogExceptionInput MakeExceptionWithStackTrace(
		const FString& Message,
		const FString& Type,
		const FString& StackTrace,
		bool bHandled = true);

	/**
	 * Converts Unreal's FFrame::GetStackTrace() text (Blueprint custom-thunk current-stack) into SDK
	 * wire order. Drops the non-frame "Script call stack:" header, trims whitespace, removes empty
	 * lines and the PostHog helper frame, then reverses the retained frames so the most recent useful
	 * caller is first. Used only by execMakeExceptionWithCurrentStack; caller-supplied raw stacks are
	 * never routed through here.
	 */
	static FString NormalizeBlueprintStackTraceForExceptionInput(const FString& RawStackTrace);

	/**
	 * Removes the leading native SDK capture frame from FPlatformStackWalk text only when the first
	 * parsed, non-empty frame positively identifies MakeExceptionWithCurrentStack, allowing for
	 * platform-specific module/address/qualified-symbol decoration. If the first frame is already an
	 * application frame, the input is returned unchanged byte-for-byte; a later matching frame is never
	 * removed.
	 */
	static FString TrimLeadingCurrentStackHelperFrame(const FString& NativeStackTrace);

	// The thunk converts the active VM frame to text immediately and stores no FFrame reference,
	// pointer, or bytecode state. The tracked script-callstack API is deliberately not used here:
	// it depends on DO_BLUEPRINT_GUARD and is not a reliable Shipping-build contract.
	DECLARE_FUNCTION(execMakeExceptionWithCurrentStack)
	{
		P_GET_PROPERTY(FStrProperty, Message);
		P_GET_PROPERTY(FStrProperty, Type);
		P_GET_UBOOL(bHandled);
		P_FINISH;

		P_NATIVE_BEGIN;
		*(FPostHogExceptionInput*)RESULT_PARAM =
			MakeExceptionWithStackTrace(Message, Type, NormalizeBlueprintStackTraceForExceptionInput(Stack.GetStackTrace()), bHandled);
		P_NATIVE_END;
	}
};
