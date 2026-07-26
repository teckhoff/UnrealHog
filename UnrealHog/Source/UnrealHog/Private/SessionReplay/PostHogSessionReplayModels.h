#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Misc/DateTime.h"
#include "Misc/Optional.h"
#include "Templates/Function.h"

/**
 * @file
 * @brief Private rrweb-compatible session replay models.
 *
 * These models own every field name, nesting shape, and timestamp unit used by session replay so
 * the later capture sources (SDKP-015, SDKP-016) and transport (SDKP-014) cannot invent their own.
 * They are deliberately private to the runtime module: nothing here is part of the public SDK
 * surface until a later task has a concrete need.
 *
 * Behavioral reference: `Design/Reference/posthog-unity/com.posthog.unity/Runtime/SessionReplay/Models/RRWebModels.cs`.
 */

/** rrweb event type discriminators. */
namespace PostHogRrwebEventType
{
	constexpr int32 FullSnapshot = 2;
	constexpr int32 IncrementalSnapshot = 3;
	constexpr int32 Meta = 4;
	constexpr int32 Plugin = 6;
}

/** rrweb pointer/touch interaction types carried inside incremental snapshots. */
namespace PostHogRrwebTouchType
{
	constexpr int32 TouchStart = 7;
	constexpr int32 TouchMove = 3;
	constexpr int32 TouchEnd = 9;
}

/** rrweb wireframe element types. Unreal captures screenshots, but the rest exist for parity. */
namespace PostHogRrwebWireframeType
{
	extern const TCHAR* const Screenshot;
	extern const TCHAR* const Text;
	extern const TCHAR* const Image;
	extern const TCHAR* const Rectangle;
	extern const TCHAR* const Input;
}

/** rrweb plugin identifiers. */
namespace PostHogRrwebPlugin
{
	extern const TCHAR* const Network;
	extern const TCHAR* const Console;
}

namespace PostHogSessionReplayTime
{
	// The single conversion from an Unreal UTC FDateTime to the Unix millisecond integer that every
	// replay model uses. Exact integer arithmetic, so serialized timestamps never gain a fraction.
	int64 ToUnixMilliseconds(const FDateTime& UtcTimestamp);
}

namespace PostHogSessionReplayJson
{
	// The single serializer for replay payloads: condensed, no pretty printing, UTF-16 text
	// preserved verbatim so non-ASCII log lines and URLs survive the round trip.
	FString Serialize(const TSharedRef<FJsonObject>& JsonObject);
}

/** Optional CSS-like style block attached to a wireframe element. Omitted fields are not serialized. */
struct FPostHogRrwebStyle
{
	TOptional<FString> Color;
	TOptional<FString> BackgroundColor;
	TOptional<int32> BorderWidth;
	TOptional<int32> BorderRadius;
	TOptional<FString> BorderColor;
	TOptional<int32> FontSize;
	TOptional<FString> FontFamily;

	TSharedRef<FJsonObject> ToJsonObject() const;
};

/** A single rrweb wireframe element. Unreal only produces the screenshot variant today. */
struct FPostHogRrwebWireframe
{
	int32 Id = 0;
	int32 X = 0;
	int32 Y = 0;
	int32 Width = 0;
	int32 Height = 0;
	FString Type;

	// Base64 image payload including its data URL prefix, e.g. "data:image/jpeg;base64,...".
	FString Base64;

	TOptional<FPostHogRrwebStyle> Style;

	static FPostHogRrwebWireframe MakeScreenshot(int32 InWidth, int32 InHeight, const FString& InBase64Data);

	TSharedRef<FJsonObject> ToJsonObject() const;
};

/** One HTTP request sample carried by the rrweb network plugin event. */
struct FPostHogRrwebNetworkSample
{
	int64 TimestampMs = 0;
	FString EntryType = TEXT("resource");
	FString InitiatorType = TEXT("fetch");
	FString Method;
	FString Name;
	int64 DurationMs = 0;
	int32 ResponseStatus = 0;
	int64 TransferSize = 0;

	TSharedRef<FJsonObject> ToJsonObject() const;
};

/** One log line carried by the rrweb console plugin event. */
struct FPostHogRrwebLogEntry
{
	int64 TimestampMs = 0;
	FString Level;
	FString Message;
	FString StackTrace;

	TSharedRef<FJsonObject> ToJsonObject() const;
};

/**
 * @brief A single rrweb event: a type discriminator, a nested data payload, and a millisecond timestamp.
 *
 * Construct these only through the factory functions so field shapes stay identical across capture sources.
 */
struct FPostHogRrwebEvent
{
	int32 Type = 0;
	int64 TimestampMs = 0;
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();

	// Screen dimensions and current screen name.
	static FPostHogRrwebEvent MakeMeta(int32 Width, int32 Height, const FString& ScreenName, int64 TimestampMs);

	// A full snapshot wrapping exactly one wireframe, matching Unity's single-screenshot model.
	static FPostHogRrwebEvent MakeFullSnapshot(const FPostHogRrwebWireframe& Wireframe, int64 TimestampMs);

	// A pointer/touch incremental snapshot. Coordinates are truncated to integer pixels.
	static FPostHogRrwebEvent MakePointer(float X, float Y, int32 TouchType, int64 TimestampMs);

	static FPostHogRrwebEvent MakeConsoleLogPlugin(const TArray<FPostHogRrwebLogEntry>& Logs, int64 TimestampMs);

	static FPostHogRrwebEvent MakeNetworkPlugin(const TArray<FPostHogRrwebNetworkSample>& Requests, int64 TimestampMs);

	TSharedRef<FJsonObject> ToJsonObject() const;
};

/**
 * @brief The `$snapshot` event envelope posted to PostHog's replay ingest.
 *
 * Carries the effective identity and session exactly once: `distinct_id` at the top level, and
 * `$session_id`/`$window_id` inside properties, where the window id is always the session id.
 */
struct FPostHogSnapshotEnvelope
{
	FString Uuid;
	FString Timestamp;
	FString DistinctId;
	FString SessionId;
	TArray<FPostHogRrwebEvent> SnapshotData;

	// Deterministic factory: the clock and UUID sources are injected so tests never depend on wall
	// time or real entropy.
	static FPostHogSnapshotEnvelope Create(const FString& InDistinctId,
		const FString& InSessionId,
		const TArray<FPostHogRrwebEvent>& InSnapshotData,
		TFunctionRef<FDateTime()> ClockSource,
		TFunctionRef<FString()> UuidGenerator);

	// Production factory: UTC wall clock plus a fresh UUIDv7.
	static FPostHogSnapshotEnvelope Create(const FString& InDistinctId,
		const FString& InSessionId,
		const TArray<FPostHogRrwebEvent>& InSnapshotData);

	TSharedRef<FJsonObject> ToJsonObject(const FString& ApiKey) const;
};
