#include "SessionReplay/PostHogSessionReplayModels.h"

#include "Dom/JsonValue.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "SDK/PostHogSdkInfo.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Utilities/PostHogUuidV7.h"

namespace PostHogRrwebWireframeType
{
	const TCHAR* const Screenshot = TEXT("screenshot");
	const TCHAR* const Text = TEXT("text");
	const TCHAR* const Image = TEXT("image");
	const TCHAR* const Rectangle = TEXT("div");
	const TCHAR* const Input = TEXT("input");
}

namespace PostHogRrwebPlugin
{
	const TCHAR* const Network = TEXT("rrweb/network@1");
	const TCHAR* const Console = TEXT("rrweb/console@1");
}

namespace
{
	// rrweb's incremental-snapshot constants for a touch-style pointer originating from a single
	// full-screen surface. Unreal has no DOM node ids, so the surface is always node 0.
	constexpr int32 PointerNodeId = 0;
	constexpr int32 PointerTypeTouch = 2;
	constexpr int32 PointerSourceTouchMove = 2;

	constexpr int32 ScreenshotWireframeId = 1;

	constexpr const TCHAR* SnapshotEventName = TEXT("$snapshot");
	constexpr const TCHAR* SnapshotSourceMobile = TEXT("mobile");

	// FJsonObject stores every number as a double. Replay timestamps and sizes are int64, so they
	// are funneled through here to keep the integral contract explicit at every call site.
	void SetIntegerField(const TSharedRef<FJsonObject>& Object, const TCHAR* Key, int64 Value)
	{
		Object->SetNumberField(Key, static_cast<double>(Value));
	}

	TSharedRef<FJsonValue> MakeStringArrayValue(const FString& SingleEntry)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		Values.Add(MakeShared<FJsonValueString>(SingleEntry));
		return MakeShared<FJsonValueArray>(Values);
	}
}

int64 PostHogSessionReplayTime::ToUnixMilliseconds(const FDateTime& UtcTimestamp)
{
	// Exact integer arithmetic against the Unix epoch, matching FDateTime::ToUnixTimestamp's own
	// definition but at millisecond resolution. Sub-millisecond ticks truncate.
	const FDateTime UnixEpoch(1970, 1, 1);
	return (UtcTimestamp.GetTicks() - UnixEpoch.GetTicks()) / ETimespan::TicksPerMillisecond;
}

FString PostHogSessionReplayJson::Serialize(const TSharedRef<FJsonObject>& JsonObject)
{
	FString Output;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(JsonObject, Writer);
	return Output;
}

TSharedRef<FJsonObject> FPostHogRrwebStyle::ToJsonObject() const
{
	const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();

	if (Color.IsSet() && !Color.GetValue().IsEmpty())
	{
		Object->SetStringField(TEXT("color"), Color.GetValue());
	}

	if (BackgroundColor.IsSet() && !BackgroundColor.GetValue().IsEmpty())
	{
		Object->SetStringField(TEXT("backgroundColor"), BackgroundColor.GetValue());
	}

	if (BorderWidth.IsSet())
	{
		SetIntegerField(Object, TEXT("borderWidth"), BorderWidth.GetValue());
	}

	if (BorderRadius.IsSet())
	{
		SetIntegerField(Object, TEXT("borderRadius"), BorderRadius.GetValue());
	}

	if (BorderColor.IsSet() && !BorderColor.GetValue().IsEmpty())
	{
		Object->SetStringField(TEXT("borderColor"), BorderColor.GetValue());
	}

	if (FontSize.IsSet())
	{
		SetIntegerField(Object, TEXT("fontSize"), FontSize.GetValue());
	}

	if (FontFamily.IsSet() && !FontFamily.GetValue().IsEmpty())
	{
		Object->SetStringField(TEXT("fontFamily"), FontFamily.GetValue());
	}

	return Object;
}

FPostHogRrwebWireframe FPostHogRrwebWireframe::MakeScreenshot(int32 InWidth, int32 InHeight, const FString& InBase64Data)
{
	FPostHogRrwebWireframe Wireframe;
	Wireframe.Id = ScreenshotWireframeId;
	Wireframe.X = 0;
	Wireframe.Y = 0;
	Wireframe.Width = InWidth;
	Wireframe.Height = InHeight;
	Wireframe.Type = PostHogRrwebWireframeType::Screenshot;
	Wireframe.Base64 = InBase64Data;
	return Wireframe;
}

TSharedRef<FJsonObject> FPostHogRrwebWireframe::ToJsonObject() const
{
	const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	SetIntegerField(Object, TEXT("id"), Id);
	SetIntegerField(Object, TEXT("x"), X);
	SetIntegerField(Object, TEXT("y"), Y);
	SetIntegerField(Object, TEXT("width"), Width);
	SetIntegerField(Object, TEXT("height"), Height);
	Object->SetStringField(TEXT("type"), Type);

	if (!Base64.IsEmpty())
	{
		Object->SetStringField(TEXT("base64"), Base64);
	}

	if (Style.IsSet())
	{
		Object->SetObjectField(TEXT("style"), Style.GetValue().ToJsonObject());
	}

	return Object;
}

TSharedRef<FJsonObject> FPostHogRrwebNetworkSample::ToJsonObject() const
{
	const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	SetIntegerField(Object, TEXT("timestamp"), TimestampMs);
	Object->SetStringField(TEXT("entryType"), EntryType);
	Object->SetStringField(TEXT("initiatorType"), InitiatorType);
	Object->SetStringField(TEXT("method"), Method.IsEmpty() ? TEXT("GET") : Method);
	Object->SetStringField(TEXT("name"), Name);
	SetIntegerField(Object, TEXT("duration"), DurationMs);
	SetIntegerField(Object, TEXT("responseStatus"), ResponseStatus);
	SetIntegerField(Object, TEXT("transferSize"), TransferSize);
	return Object;
}

TSharedRef<FJsonObject> FPostHogRrwebLogEntry::ToJsonObject() const
{
	const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	SetIntegerField(Object, TEXT("timestamp"), TimestampMs);
	Object->SetStringField(TEXT("level"), Level.IsEmpty() ? TEXT("log") : Level);
	Object->SetField(TEXT("payload"), MakeStringArrayValue(Message));

	if (!StackTrace.IsEmpty())
	{
		Object->SetField(TEXT("trace"), MakeStringArrayValue(StackTrace));
	}

	return Object;
}

FPostHogRrwebEvent FPostHogRrwebEvent::MakeMeta(int32 Width, int32 Height, const FString& ScreenName, int64 TimestampMs)
{
	FPostHogRrwebEvent Event;
	Event.Type = PostHogRrwebEventType::Meta;
	Event.TimestampMs = TimestampMs;
	Event.Data = MakeShared<FJsonObject>();
	SetIntegerField(Event.Data, TEXT("width"), Width);
	SetIntegerField(Event.Data, TEXT("height"), Height);
	Event.Data->SetStringField(TEXT("href"), ScreenName);
	return Event;
}

FPostHogRrwebEvent FPostHogRrwebEvent::MakeFullSnapshot(const FPostHogRrwebWireframe& Wireframe, int64 TimestampMs)
{
	const TSharedRef<FJsonObject> InitialOffset = MakeShared<FJsonObject>();
	SetIntegerField(InitialOffset, TEXT("top"), 0);
	SetIntegerField(InitialOffset, TEXT("left"), 0);

	TArray<TSharedPtr<FJsonValue>> Wireframes;
	Wireframes.Add(MakeShared<FJsonValueObject>(Wireframe.ToJsonObject()));

	FPostHogRrwebEvent Event;
	Event.Type = PostHogRrwebEventType::FullSnapshot;
	Event.TimestampMs = TimestampMs;
	Event.Data = MakeShared<FJsonObject>();
	Event.Data->SetObjectField(TEXT("initialOffset"), InitialOffset);
	Event.Data->SetArrayField(TEXT("wireframes"), Wireframes);
	return Event;
}

FPostHogRrwebEvent FPostHogRrwebEvent::MakePointer(float X, float Y, int32 TouchType, int64 TimestampMs)
{
	FPostHogRrwebEvent Event;
	Event.Type = PostHogRrwebEventType::IncrementalSnapshot;
	Event.TimestampMs = TimestampMs;
	Event.Data = MakeShared<FJsonObject>();
	SetIntegerField(Event.Data, TEXT("id"), PointerNodeId);
	SetIntegerField(Event.Data, TEXT("pointerType"), PointerTypeTouch);
	SetIntegerField(Event.Data, TEXT("source"), PointerSourceTouchMove);
	SetIntegerField(Event.Data, TEXT("type"), TouchType);
	SetIntegerField(Event.Data, TEXT("x"), static_cast<int64>(static_cast<int32>(X)));
	SetIntegerField(Event.Data, TEXT("y"), static_cast<int64>(static_cast<int32>(Y)));
	return Event;
}

FPostHogRrwebEvent FPostHogRrwebEvent::MakeConsoleLogPlugin(const TArray<FPostHogRrwebLogEntry>& Logs, int64 TimestampMs)
{
	TArray<TSharedPtr<FJsonValue>> LogValues;
	for (const FPostHogRrwebLogEntry& Log : Logs)
	{
		LogValues.Add(MakeShared<FJsonValueObject>(Log.ToJsonObject()));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetArrayField(TEXT("logs"), LogValues);

	FPostHogRrwebEvent Event;
	Event.Type = PostHogRrwebEventType::Plugin;
	Event.TimestampMs = TimestampMs;
	Event.Data = MakeShared<FJsonObject>();
	Event.Data->SetStringField(TEXT("plugin"), PostHogRrwebPlugin::Console);
	Event.Data->SetObjectField(TEXT("payload"), Payload);
	return Event;
}

FPostHogRrwebEvent FPostHogRrwebEvent::MakeNetworkPlugin(const TArray<FPostHogRrwebNetworkSample>& Requests, int64 TimestampMs)
{
	TArray<TSharedPtr<FJsonValue>> RequestValues;
	for (const FPostHogRrwebNetworkSample& Request : Requests)
	{
		RequestValues.Add(MakeShared<FJsonValueObject>(Request.ToJsonObject()));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetArrayField(TEXT("requests"), RequestValues);

	FPostHogRrwebEvent Event;
	Event.Type = PostHogRrwebEventType::Plugin;
	Event.TimestampMs = TimestampMs;
	Event.Data = MakeShared<FJsonObject>();
	Event.Data->SetStringField(TEXT("plugin"), PostHogRrwebPlugin::Network);
	Event.Data->SetObjectField(TEXT("payload"), Payload);
	return Event;
}

TSharedRef<FJsonObject> FPostHogRrwebEvent::ToJsonObject() const
{
	const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	SetIntegerField(Object, TEXT("type"), Type);
	Object->SetObjectField(TEXT("data"), Data);
	SetIntegerField(Object, TEXT("timestamp"), TimestampMs);
	return Object;
}

FPostHogSnapshotEnvelope FPostHogSnapshotEnvelope::Create(const FString& InDistinctId,
	const FString& InSessionId,
	const TArray<FPostHogRrwebEvent>& InSnapshotData,
	TFunctionRef<FDateTime()> ClockSource,
	TFunctionRef<FString()> UuidGenerator)
{
	FPostHogSnapshotEnvelope Envelope;
	Envelope.Uuid = UuidGenerator();
	Envelope.Timestamp = ClockSource().ToIso8601();
	Envelope.DistinctId = InDistinctId;
	Envelope.SessionId = InSessionId;
	Envelope.SnapshotData = InSnapshotData;
	return Envelope;
}

FPostHogSnapshotEnvelope FPostHogSnapshotEnvelope::Create(const FString& InDistinctId,
	const FString& InSessionId,
	const TArray<FPostHogRrwebEvent>& InSnapshotData)
{
	auto SystemClock = []() { return FDateTime::UtcNow(); };
	auto UuidV7 = []() { return PostHogUuidV7::New(); };
	return Create(InDistinctId, InSessionId, InSnapshotData, SystemClock, UuidV7);
}

TSharedRef<FJsonObject> FPostHogSnapshotEnvelope::ToJsonObject(const FString& ApiKey) const
{
	TArray<TSharedPtr<FJsonValue>> SnapshotValues;
	for (const FPostHogRrwebEvent& Event : SnapshotData)
	{
		SnapshotValues.Add(MakeShared<FJsonValueObject>(Event.ToJsonObject()));
	}

	// $window_id intentionally mirrors $session_id: PostHog's replay player requires a window id,
	// and an Unreal process has exactly one replay surface per session.
	const TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
	Properties->SetStringField(TEXT("$snapshot_source"), SnapshotSourceMobile);
	Properties->SetStringField(TEXT("$session_id"), SessionId);
	Properties->SetStringField(TEXT("$window_id"), SessionId);
	Properties->SetArrayField(TEXT("$snapshot_data"), SnapshotValues);
	Properties->SetStringField(TEXT("$lib"), FPostHogSdkInfo::GetLibraryName());
	Properties->SetStringField(TEXT("$lib_version"), FPostHogSdkInfo::GetPluginVersion());

	const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("uuid"), Uuid);
	Object->SetStringField(TEXT("event"), SnapshotEventName);
	Object->SetStringField(TEXT("distinct_id"), DistinctId);
	Object->SetStringField(TEXT("timestamp"), Timestamp);
	Object->SetStringField(TEXT("api_key"), ApiKey);
	Object->SetObjectField(TEXT("properties"), Properties);
	return Object;
}
