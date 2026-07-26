#include "SessionReplay/PostHogSessionReplayModels.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "Misc/DateTime.h"
#include "SDK/PostHogSdkInfo.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Utilities/PostHogUuidV7.h"

namespace PostHogReplayModelTests
{
	// Deterministic capture instant shared by every fixture in this file.
	const FDateTime FixedUtc(2026, 7, 26, 18, 30, 45, 123);
	constexpr int64 FixedUnixMilliseconds = 1785090645123LL;

	FString FixedUuid()
	{
		return PostHogUuidV7::Pack(1645557742000ULL, 0xCC3, 0x18C4DC0C0C07398FULL);
	}

	TSharedPtr<FJsonObject> ParseObject(const FString& Json)
	{
		TSharedPtr<FJsonObject> Parsed;
		const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Json);
		FJsonSerializer::Deserialize(Reader, Parsed);
		return Parsed;
	}

	// Structural comparison so fixtures assert field shape and values rather than key ordering.
	bool JsonValuesEqual(const TSharedPtr<FJsonValue>& Lhs, const TSharedPtr<FJsonValue>& Rhs);

	bool JsonObjectsEqual(const TSharedPtr<FJsonObject>& Lhs, const TSharedPtr<FJsonObject>& Rhs)
	{
		if (!Lhs.IsValid() || !Rhs.IsValid())
		{
			return Lhs.IsValid() == Rhs.IsValid();
		}

		if (Lhs->Values.Num() != Rhs->Values.Num())
		{
			return false;
		}

		for (const auto& Pair : Lhs->Values)
		{
			const TSharedPtr<FJsonValue> Other = Rhs->TryGetField(Pair.Key);
			if (!Other.IsValid() || !JsonValuesEqual(Pair.Value, Other))
			{
				return false;
			}
		}

		return true;
	}

	bool JsonValuesEqual(const TSharedPtr<FJsonValue>& Lhs, const TSharedPtr<FJsonValue>& Rhs)
	{
		if (!Lhs.IsValid() || !Rhs.IsValid())
		{
			return Lhs.IsValid() == Rhs.IsValid();
		}

		if (Lhs->Type != Rhs->Type)
		{
			return false;
		}

		switch (Lhs->Type)
		{
		case EJson::Object:
			return JsonObjectsEqual(Lhs->AsObject(), Rhs->AsObject());
		case EJson::Array:
		{
			const TArray<TSharedPtr<FJsonValue>>& LhsArray = Lhs->AsArray();
			const TArray<TSharedPtr<FJsonValue>>& RhsArray = Rhs->AsArray();
			if (LhsArray.Num() != RhsArray.Num())
			{
				return false;
			}
			for (int32 Index = 0; Index < LhsArray.Num(); ++Index)
			{
				if (!JsonValuesEqual(LhsArray[Index], RhsArray[Index]))
				{
					return false;
				}
			}
			return true;
		}
		case EJson::String:
			return Lhs->AsString() == Rhs->AsString();
		case EJson::Number:
			return Lhs->AsNumber() == Rhs->AsNumber();
		case EJson::Boolean:
			return Lhs->AsBool() == Rhs->AsBool();
		default:
			return true;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogSessionReplayUnixMillisecondsTest, "UnrealHog.SessionReplay.Models.UnixMilliseconds", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogSessionReplayUnixMillisecondsTest::RunTest(const FString& Parameters)
{
	using namespace PostHogReplayModelTests;

	TestEqual(TEXT("Unix epoch converts to zero"), PostHogSessionReplayTime::ToUnixMilliseconds(FDateTime(1970, 1, 1)), static_cast<int64>(0));
	TestEqual(TEXT("Fixed instant converts to its millisecond value"), PostHogSessionReplayTime::ToUnixMilliseconds(FixedUtc), FixedUnixMilliseconds);

	// Sub-millisecond ticks truncate rather than rounding into the next millisecond.
	const FDateTime FixedWithExtraTicks(FixedUtc.GetTicks() + 9999);
	TestEqual(TEXT("Sub-millisecond ticks truncate"), PostHogSessionReplayTime::ToUnixMilliseconds(FixedWithExtraTicks), FixedUnixMilliseconds);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogSessionReplayRrwebJsonFixturesTest, "UnrealHog.SessionReplay.Models.RrwebJsonFixtures", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogSessionReplayRrwebJsonFixturesTest::RunTest(const FString& Parameters)
{
	using namespace PostHogReplayModelTests;

	const int64 Timestamp = PostHogSessionReplayTime::ToUnixMilliseconds(FixedUtc);

	// Meta event.
	{
		const FPostHogRrwebEvent Event = FPostHogRrwebEvent::MakeMeta(1920, 1080, TEXT("MainMenu"), Timestamp);
		const FString Actual = PostHogSessionReplayJson::Serialize(Event.ToJsonObject());
		const FString Fixture = TEXT("{\"type\":4,\"data\":{\"width\":1920,\"height\":1080,\"href\":\"MainMenu\"},\"timestamp\":1785090645123}");

		TestTrue(TEXT("Meta event matches fixture"), JsonObjectsEqual(ParseObject(Actual), ParseObject(Fixture)));
		TestTrue(TEXT("Meta timestamp is an integer token"), Actual.Contains(TEXT("\"timestamp\":1785090645123")));
		TestFalse(TEXT("Meta timestamp has no fraction"), Actual.Contains(TEXT("1785090645123.")));
	}

	// Full snapshot event with a screenshot wireframe.
	{
		const FPostHogRrwebWireframe Wireframe = FPostHogRrwebWireframe::MakeScreenshot(960, 540, TEXT("data:image/jpeg;base64,QUJD"));
		const FPostHogRrwebEvent Event = FPostHogRrwebEvent::MakeFullSnapshot(Wireframe, Timestamp);
		const FString Actual = PostHogSessionReplayJson::Serialize(Event.ToJsonObject());
		const FString Fixture = TEXT("{\"type\":2,\"data\":{\"initialOffset\":{\"top\":0,\"left\":0},\"wireframes\":[{\"id\":1,\"x\":0,\"y\":0,\"width\":960,\"height\":540,\"type\":\"screenshot\",\"base64\":\"data:image/jpeg;base64,QUJD\"}]},\"timestamp\":1785090645123}");

		TestTrue(TEXT("Full snapshot matches fixture"), JsonObjectsEqual(ParseObject(Actual), ParseObject(Fixture)));
		TestFalse(TEXT("Absent style is omitted"), Actual.Contains(TEXT("\"style\"")));
	}

	// Full snapshot with an explicit style block: only the set style fields serialize.
	{
		FPostHogRrwebWireframe Wireframe = FPostHogRrwebWireframe::MakeScreenshot(2, 3, TEXT("data:image/jpeg;base64,QQ=="));
		FPostHogRrwebStyle Style;
		Style.BackgroundColor = TEXT("#101010");
		Style.BorderRadius = 4;
		Wireframe.Style = Style;

		const FString Actual = PostHogSessionReplayJson::Serialize(FPostHogRrwebEvent::MakeFullSnapshot(Wireframe, Timestamp).ToJsonObject());
		const FString Fixture = TEXT("{\"type\":2,\"data\":{\"initialOffset\":{\"top\":0,\"left\":0},\"wireframes\":[{\"id\":1,\"x\":0,\"y\":0,\"width\":2,\"height\":3,\"type\":\"screenshot\",\"base64\":\"data:image/jpeg;base64,QQ==\",\"style\":{\"backgroundColor\":\"#101010\",\"borderRadius\":4}}]},\"timestamp\":1785090645123}");

		TestTrue(TEXT("Styled wireframe matches fixture"), JsonObjectsEqual(ParseObject(Actual), ParseObject(Fixture)));
	}

	// Pointer/touch incremental snapshots.
	{
		const FPostHogRrwebEvent Down = FPostHogRrwebEvent::MakePointer(120.9f, 240.4f, PostHogRrwebTouchType::TouchStart, Timestamp);
		const FString ActualDown = PostHogSessionReplayJson::Serialize(Down.ToJsonObject());
		const FString FixtureDown = TEXT("{\"type\":3,\"data\":{\"id\":0,\"pointerType\":2,\"source\":2,\"type\":7,\"x\":120,\"y\":240},\"timestamp\":1785090645123}");

		TestTrue(TEXT("Touch start matches fixture"), JsonObjectsEqual(ParseObject(ActualDown), ParseObject(FixtureDown)));
		TestTrue(TEXT("Pointer coordinates truncate to integers"), ActualDown.Contains(TEXT("\"x\":120")) && ActualDown.Contains(TEXT("\"y\":240")));

		const FPostHogRrwebEvent Move = FPostHogRrwebEvent::MakePointer(0.0f, 0.0f, PostHogRrwebTouchType::TouchMove, Timestamp);
		TestTrue(TEXT("Touch move matches fixture"), JsonObjectsEqual(
			ParseObject(PostHogSessionReplayJson::Serialize(Move.ToJsonObject())),
			ParseObject(TEXT("{\"type\":3,\"data\":{\"id\":0,\"pointerType\":2,\"source\":2,\"type\":3,\"x\":0,\"y\":0},\"timestamp\":1785090645123}"))));

		const FPostHogRrwebEvent Up = FPostHogRrwebEvent::MakePointer(-5.7f, 8.2f, PostHogRrwebTouchType::TouchEnd, Timestamp);
		TestTrue(TEXT("Touch end matches fixture"), JsonObjectsEqual(
			ParseObject(PostHogSessionReplayJson::Serialize(Up.ToJsonObject())),
			ParseObject(TEXT("{\"type\":3,\"data\":{\"id\":0,\"pointerType\":2,\"source\":2,\"type\":9,\"x\":-5,\"y\":8},\"timestamp\":1785090645123}"))));
	}

	// Console-log plugin event.
	{
		FPostHogRrwebLogEntry WithTrace;
		WithTrace.TimestampMs = Timestamp;
		WithTrace.Level = TEXT("error");
		WithTrace.Message = TEXT("Boom");
		WithTrace.StackTrace = TEXT("Frame0\nFrame1");

		FPostHogRrwebLogEntry WithoutTrace;
		WithoutTrace.TimestampMs = Timestamp;
		WithoutTrace.Level = TEXT("warn");
		WithoutTrace.Message = TEXT("Careful");

		const FString Actual = PostHogSessionReplayJson::Serialize(FPostHogRrwebEvent::MakeConsoleLogPlugin({ WithTrace, WithoutTrace }, Timestamp).ToJsonObject());
		const FString Fixture = TEXT("{\"type\":6,\"data\":{\"plugin\":\"rrweb/console@1\",\"payload\":{\"logs\":[")
			TEXT("{\"timestamp\":1785090645123,\"level\":\"error\",\"payload\":[\"Boom\"],\"trace\":[\"Frame0\\nFrame1\"]},")
			TEXT("{\"timestamp\":1785090645123,\"level\":\"warn\",\"payload\":[\"Careful\"]}")
			TEXT("]}},\"timestamp\":1785090645123}");

		TestTrue(TEXT("Console plugin matches fixture"), JsonObjectsEqual(ParseObject(Actual), ParseObject(Fixture)));

		// An unset level falls back to "log" rather than emitting an empty string.
		FPostHogRrwebLogEntry Defaulted;
		Defaulted.TimestampMs = Timestamp;
		Defaulted.Message = TEXT("plain");
		const FString DefaultedActual = PostHogSessionReplayJson::Serialize(FPostHogRrwebEvent::MakeConsoleLogPlugin({ Defaulted }, Timestamp).ToJsonObject());
		TestTrue(TEXT("Missing log level defaults to log"), DefaultedActual.Contains(TEXT("\"level\":\"log\"")));
	}

	// Network plugin event.
	{
		FPostHogRrwebNetworkSample Sample;
		Sample.TimestampMs = Timestamp;
		Sample.Method = TEXT("POST");
		Sample.Name = TEXT("https://us.i.posthog.com/batch");
		Sample.DurationMs = 42;
		Sample.ResponseStatus = 200;
		Sample.TransferSize = 1024;

		FPostHogRrwebNetworkSample Defaulted;
		Defaulted.TimestampMs = Timestamp;

		const FString Actual = PostHogSessionReplayJson::Serialize(FPostHogRrwebEvent::MakeNetworkPlugin({ Sample, Defaulted }, Timestamp).ToJsonObject());
		const FString Fixture = TEXT("{\"type\":6,\"data\":{\"plugin\":\"rrweb/network@1\",\"payload\":{\"requests\":[")
			TEXT("{\"timestamp\":1785090645123,\"entryType\":\"resource\",\"initiatorType\":\"fetch\",\"method\":\"POST\",\"name\":\"https://us.i.posthog.com/batch\",\"duration\":42,\"responseStatus\":200,\"transferSize\":1024},")
			TEXT("{\"timestamp\":1785090645123,\"entryType\":\"resource\",\"initiatorType\":\"fetch\",\"method\":\"GET\",\"name\":\"\",\"duration\":0,\"responseStatus\":0,\"transferSize\":0}")
			TEXT("]}},\"timestamp\":1785090645123}");

		TestTrue(TEXT("Network plugin matches fixture"), JsonObjectsEqual(ParseObject(Actual), ParseObject(Fixture)));
		TestTrue(TEXT("Network duration is an integer token"), Actual.Contains(TEXT("\"duration\":42")));
		TestFalse(TEXT("Network duration has no fraction"), Actual.Contains(TEXT("\"duration\":42.")));
	}

	// Empty plugin payloads still serialize their container arrays.
	{
		const FString EmptyConsole = PostHogSessionReplayJson::Serialize(FPostHogRrwebEvent::MakeConsoleLogPlugin({}, Timestamp).ToJsonObject());
		TestTrue(TEXT("Empty console payload keeps its array"), EmptyConsole.Contains(TEXT("\"logs\":[]")));

		const FString EmptyNetwork = PostHogSessionReplayJson::Serialize(FPostHogRrwebEvent::MakeNetworkPlugin({}, Timestamp).ToJsonObject());
		TestTrue(TEXT("Empty network payload keeps its array"), EmptyNetwork.Contains(TEXT("\"requests\":[]")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogSessionReplayNonAsciiRoundTripTest, "UnrealHog.SessionReplay.Models.NonAsciiRoundTrip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogSessionReplayNonAsciiRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace PostHogReplayModelTests;

	const FString NonAsciiScreen = TEXT("主メニュー");
	const FString NonAsciiMessage = TEXT("échec: тест — über");
	const FString NonAsciiUrl = TEXT("https://example.com/ça/va?q=日本語");

	const int64 Timestamp = PostHogSessionReplayTime::ToUnixMilliseconds(FixedUtc);

	const FString MetaJson = PostHogSessionReplayJson::Serialize(FPostHogRrwebEvent::MakeMeta(800, 600, NonAsciiScreen, Timestamp).ToJsonObject());
	const TSharedPtr<FJsonObject> ParsedMeta = ParseObject(MetaJson);
	if (!TestTrue(TEXT("Meta round-trips as valid JSON"), ParsedMeta.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("Non-ASCII screen name preserved"), ParsedMeta->GetObjectField(TEXT("data"))->GetStringField(TEXT("href")), NonAsciiScreen);

	FPostHogRrwebLogEntry Log;
	Log.TimestampMs = Timestamp;
	Log.Level = TEXT("error");
	Log.Message = NonAsciiMessage;

	const FString ConsoleJson = PostHogSessionReplayJson::Serialize(FPostHogRrwebEvent::MakeConsoleLogPlugin({ Log }, Timestamp).ToJsonObject());
	const TSharedPtr<FJsonObject> ParsedConsole = ParseObject(ConsoleJson);
	if (!TestTrue(TEXT("Console round-trips as valid JSON"), ParsedConsole.IsValid()))
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>& Logs = ParsedConsole->GetObjectField(TEXT("data"))->GetObjectField(TEXT("payload"))->GetArrayField(TEXT("logs"));
	if (!TestEqual(TEXT("One log entry survives"), Logs.Num(), 1))
	{
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>& Payload = Logs[0]->AsObject()->GetArrayField(TEXT("payload"));
	if (!TestEqual(TEXT("One log payload line survives"), Payload.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("Non-ASCII log message preserved"), Payload[0]->AsString(), NonAsciiMessage);

	FPostHogRrwebNetworkSample Sample;
	Sample.TimestampMs = Timestamp;
	Sample.Name = NonAsciiUrl;

	const FString NetworkJson = PostHogSessionReplayJson::Serialize(FPostHogRrwebEvent::MakeNetworkPlugin({ Sample }, Timestamp).ToJsonObject());
	const TSharedPtr<FJsonObject> ParsedNetwork = ParseObject(NetworkJson);
	if (!TestTrue(TEXT("Network round-trips as valid JSON"), ParsedNetwork.IsValid()))
	{
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>& Requests = ParsedNetwork->GetObjectField(TEXT("data"))->GetObjectField(TEXT("payload"))->GetArrayField(TEXT("requests"));
	if (!TestEqual(TEXT("One request survives"), Requests.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("Non-ASCII URL preserved"), Requests[0]->AsObject()->GetStringField(TEXT("name")), NonAsciiUrl);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogSessionReplaySnapshotEnvelopeFixtureTest, "UnrealHog.SessionReplay.Models.SnapshotEnvelopeFixture", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogSessionReplaySnapshotEnvelopeFixtureTest::RunTest(const FString& Parameters)
{
	using namespace PostHogReplayModelTests;

	const int64 Timestamp = PostHogSessionReplayTime::ToUnixMilliseconds(FixedUtc);
	const FString DistinctId = TEXT("distinct-under-test");
	const FString SessionId = TEXT("session-under-test");

	TArray<FPostHogRrwebEvent> SnapshotData;
	SnapshotData.Add(FPostHogRrwebEvent::MakeMeta(1920, 1080, TEXT("MainMenu"), Timestamp));
	SnapshotData.Add(FPostHogRrwebEvent::MakeFullSnapshot(FPostHogRrwebWireframe::MakeScreenshot(960, 540, TEXT("data:image/jpeg;base64,QUJD")), Timestamp));

	auto Clock = [] { return FixedUtc; };
	auto Uuid = [] { return FixedUuid(); };

	const FPostHogSnapshotEnvelope Envelope = FPostHogSnapshotEnvelope::Create(DistinctId, SessionId, SnapshotData, Clock, Uuid);

	TestEqual(TEXT("Injected UUID is used verbatim"), Envelope.Uuid, FixedUuid());
	TestEqual(TEXT("Injected clock drives the ISO timestamp"), Envelope.Timestamp, FixedUtc.ToIso8601());

	const TSharedRef<FJsonObject> Json = Envelope.ToJsonObject(TEXT("phc_replay_test"));
	const FString Serialized = PostHogSessionReplayJson::Serialize(Json);

	const FString ExpectedFixture = FString::Printf(
		TEXT("{\"uuid\":\"%s\",\"event\":\"$snapshot\",\"distinct_id\":\"%s\",\"timestamp\":\"%s\",\"api_key\":\"phc_replay_test\",")
		TEXT("\"properties\":{\"$snapshot_source\":\"mobile\",\"$session_id\":\"%s\",\"$window_id\":\"%s\",")
		TEXT("\"$snapshot_data\":[")
		TEXT("{\"type\":4,\"data\":{\"width\":1920,\"height\":1080,\"href\":\"MainMenu\"},\"timestamp\":1785090645123},")
		TEXT("{\"type\":2,\"data\":{\"initialOffset\":{\"top\":0,\"left\":0},\"wireframes\":[{\"id\":1,\"x\":0,\"y\":0,\"width\":960,\"height\":540,\"type\":\"screenshot\",\"base64\":\"data:image/jpeg;base64,QUJD\"}]},\"timestamp\":1785090645123}")
		TEXT("],\"$lib\":\"%s\",\"$lib_version\":\"%s\"}}"),
		*FixedUuid(),
		*DistinctId,
		*FixedUtc.ToIso8601(),
		*SessionId,
		*SessionId,
		*FPostHogSdkInfo::GetLibraryName(),
		*FPostHogSdkInfo::GetPluginVersion());

	TestTrue(TEXT("Envelope matches fixture"), JsonObjectsEqual(ParseObject(Serialized), ParseObject(ExpectedFixture)));

	// Identity and session appear exactly once each, in exactly one place.
	{
		int32 DistinctIdKeyCount = 0;
		int32 SearchFrom = 0;
		while (true)
		{
			const int32 Found = Serialized.Find(TEXT("\"distinct_id\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchFrom);
			if (Found == INDEX_NONE)
			{
				break;
			}
			++DistinctIdKeyCount;
			SearchFrom = Found + 1;
		}
		TestEqual(TEXT("distinct_id appears exactly once"), DistinctIdKeyCount, 1);
	}

	TestTrue(TEXT("distinct_id is top level"), Json->GetStringField(TEXT("distinct_id")) == DistinctId);

	const TSharedPtr<FJsonObject> Properties = Json->GetObjectField(TEXT("properties"));
	TestFalse(TEXT("Properties do not repeat distinct_id"), Properties->HasField(TEXT("distinct_id")));
	TestEqual(TEXT("Session id is present"), Properties->GetStringField(TEXT("$session_id")), SessionId);
	TestEqual(TEXT("Window id mirrors the session id"), Properties->GetStringField(TEXT("$window_id")), Properties->GetStringField(TEXT("$session_id")));
	TestEqual(TEXT("Snapshot source is mobile"), Properties->GetStringField(TEXT("$snapshot_source")), FString(TEXT("mobile")));
	TestEqual(TEXT("SDK library metadata is present"), Properties->GetStringField(TEXT("$lib")), FPostHogSdkInfo::GetLibraryName());
	TestEqual(TEXT("SDK version metadata is present"), Properties->GetStringField(TEXT("$lib_version")), FPostHogSdkInfo::GetPluginVersion());
	TestEqual(TEXT("Snapshot data preserves every event"), Properties->GetArrayField(TEXT("$snapshot_data")).Num(), 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogSessionReplaySnapshotEnvelopeDefaultsTest, "UnrealHog.SessionReplay.Models.SnapshotEnvelopeDefaults", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogSessionReplaySnapshotEnvelopeDefaultsTest::RunTest(const FString& Parameters)
{
	const FPostHogSnapshotEnvelope Envelope = FPostHogSnapshotEnvelope::Create(TEXT("distinct"), TEXT("session"), {});

	// The production factory must still produce a UUIDv7 and a parseable ISO 8601 timestamp.
	TestEqual(TEXT("Generated UUID has canonical length"), Envelope.Uuid.Len(), 36);
	TestEqual(TEXT("Generated UUID declares version 7"), Envelope.Uuid[14], TCHAR('7'));

	FDateTime ParsedTimestamp;
	TestTrue(TEXT("Generated timestamp is ISO 8601"), FDateTime::ParseIso8601(*Envelope.Timestamp, ParsedTimestamp));

	const TSharedRef<FJsonObject> Json = Envelope.ToJsonObject(TEXT("phc_replay_test"));
	TestEqual(TEXT("Empty snapshot data still serializes an array"), Json->GetObjectField(TEXT("properties"))->GetArrayField(TEXT("$snapshot_data")).Num(), 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
