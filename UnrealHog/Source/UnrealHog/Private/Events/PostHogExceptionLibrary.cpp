#include "Events/PostHogExceptionLibrary.h"

#include "HAL/PlatformStackWalk.h"

namespace
{
	// Best-effort native stack capture. FORCENOINLINE so Shipping optimization is less likely to
	// fold this into its caller and make ExtraFramesToIgnore misleading; the ignore depth is
	// advisory, not a behavior contract. May return empty text depending on platform,
	// optimization, and symbol packaging - callers must preserve that emptiness.
	FORCENOINLINE FString CaptureNativeStackTrace(int32 ExtraFramesToIgnore)
	{
		static constexpr int32 StackTraceSize = 65536;

		TArray<ANSICHAR> StackTrace;
		StackTrace.SetNumZeroed(StackTraceSize);

		FPlatformStackWalk::InitStackWalking();
		FPlatformStackWalk::StackWalkAndDump(
			StackTrace.GetData(),
			StackTrace.Num(),
			ExtraFramesToIgnore + 1);

		return FString(ANSI_TO_TCHAR(StackTrace.GetData()));
	}
}

FPostHogExceptionInput UPostHogExceptionLibrary::MakeExceptionWithStackTrace(
	const FString& Message,
	const FString& Type,
	const FString& StackTrace,
	bool bHandled)
{
	// Verbatim copy: no trimming, normalizing, or validation. Blank checks belong to
	// CaptureException, and empty stack text stays empty rather than gaining invented frames.
	FPostHogExceptionInput Exception;
	Exception.Message = Message;
	Exception.Type = Type;
	Exception.StackTrace = StackTrace;
	Exception.bHandled = bHandled;
	return Exception;
}

FPostHogExceptionInput UPostHogExceptionLibrary::MakeExceptionWithNativeStack(
	const FString& Message,
	const FString& Type,
	bool bHandled)
{
	return MakeExceptionWithStackTrace(Message, Type, CaptureNativeStackTrace(1), bHandled);
}

FPostHogExceptionInput UPostHogExceptionLibrary::MakeExceptionWithCurrentStack(
	const FString& Message,
	const FString& Type,
	bool bHandled)
{
	// Trim the SDK capture wrapper (this function) from the top of the reported stack so the
	// caller becomes the first application frame. Best-effort: optimized builds may still drop
	// intermediate frames. MakeExceptionWithNativeStack keeps its own skip depth (1).
	return MakeExceptionWithStackTrace(
		Message,
		Type,
		CaptureNativeStackTrace(2),
		bHandled);
}
