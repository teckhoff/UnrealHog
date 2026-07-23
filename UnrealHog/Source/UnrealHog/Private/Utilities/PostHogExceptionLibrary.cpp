#include "Utilities/PostHogExceptionLibrary.h"

#include "Algo/Reverse.h"
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

FString UPostHogExceptionLibrary::NormalizeBlueprintStackTraceForExceptionInput(const FString& RawStackTrace)
{
	// Engine text arrives oldest-to-newest (FFrame::GetStackTrace emits ReverseIterate over the
	// PreviousFrame walk) prefixed with a non-frame "Script call stack:" header. Retain only useful
	// frame lines, then reverse once so the most recent caller becomes the SDK wire-order frame zero.
	TArray<FString> Lines;
	RawStackTrace.ParseIntoArrayLines(Lines, /*bCullEmpty=*/false);

	TArray<FString> Retained;
	Retained.Reserve(Lines.Num());
	for (const FString& Line : Lines)
	{
		const FString Trimmed = Line.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			continue;
		}
		if (Trimmed.StartsWith(TEXT("Script call stack:")))
		{
			continue;
		}
		// Drop the PostHog SDK implementation frame so the first retained frame is user code.
		if (Trimmed.Contains(TEXT("MakeExceptionWithCurrentStack")))
		{
			continue;
		}
		Retained.Add(Trimmed);
	}

	Algo::Reverse(Retained);
	return FString::Join(Retained, LINE_TERMINATOR);
}

FString UPostHogExceptionLibrary::TrimLeadingCurrentStackHelperFrame(const FString& NativeStackTrace)
{
	// Content-based trim at the current-stack capture boundary: remove exactly one frame, and only
	// when the first parsed non-empty frame is positively the SDK helper. Numeric skip depth is not a
	// reliable frame boundary, so index-zero identity is the sole trigger; deeper matches are ignored.
	TArray<FString> Lines;
	NativeStackTrace.ParseIntoArrayLines(Lines, /*bCullEmpty=*/false);

	int32 FirstNonEmptyIndex = INDEX_NONE;
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		if (!Lines[Index].TrimStartAndEnd().IsEmpty())
		{
			FirstNonEmptyIndex = Index;
			break;
		}
	}

	if (FirstNonEmptyIndex == INDEX_NONE)
	{
		return NativeStackTrace;
	}

	if (!Lines[FirstNonEmptyIndex].Contains(TEXT("MakeExceptionWithCurrentStack")))
	{
		return NativeStackTrace;
	}

	Lines.RemoveAt(FirstNonEmptyIndex);
	return FString::Join(Lines, LINE_TERMINATOR);
}

FPostHogExceptionInput UPostHogExceptionLibrary::MakeExceptionWithCurrentStack(
	const FString& Message,
	const FString& Type,
	bool bHandled)
{
	// Capture without adding another unconditional ignored frame (that has been observed to discard
	// the newest useful application frame), then content-trim only a positively identified leading
	// SDK helper frame.
	const FString RawStack = CaptureNativeStackTrace(1);
	return MakeExceptionWithStackTrace(Message, Type, TrimLeadingCurrentStackHelperFrame(RawStack), bHandled);
}
