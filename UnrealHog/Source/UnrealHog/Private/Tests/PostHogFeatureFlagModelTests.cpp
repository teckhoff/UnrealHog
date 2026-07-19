#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "FeatureFlags/PostHogFlagValue.h"
#include "FeatureFlags/Models/PostHogFeatureFlag.h"
#include "FeatureFlags/Models/PostHogFeatureFlagsResponse.h"

namespace
{
	/** Parses an inline JSON object fixture. Returns null on parse failure or non-object roots. */
	TSharedPtr<FJsonObject> ParseObject(const FString& Json)
	{
		TSharedPtr<FJsonObject> Object;
		const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, Object) || !Object.IsValid())
		{
			return nullptr;
		}
		return Object;
	}

	/** Serializes a JSON object to a compact string. */
	FString SerializeObject(const TSharedRef<FJsonObject>& Object)
	{
		FString Output;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
		FJsonSerializer::Serialize(Object, Writer);
		return Output;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFlagValueTypesTest, "UnrealHog.FeatureFlags.Models.ResolveValueTypes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFlagValueTypesTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"featureFlags": {
			"bool-true": true,
			"bool-false": false,
			"variant": "control",
			"empty-variant": ""
		}
	})");

	const TSharedPtr<FJsonObject> Object = ParseObject(Json);
	TestTrue(TEXT("fixture parses"), Object.IsValid());

	const TOptional<FPostHogFeatureFlagsResponse> Response = FPostHogFeatureFlagsResponse::FromJson(Object);
	TestTrue(TEXT("response parsed"), Response.IsSet());

	const FPostHogFlagValue BoolTrue = Response->ResolveValue(TEXT("bool-true"));
	TestTrue(TEXT("bool-true is bool"), BoolTrue.IsBool());
	TestTrue(TEXT("bool-true enabled"), BoolTrue.IsEnabled());
	TestTrue(TEXT("bool-true value"), BoolTrue.bBoolValue);

	const FPostHogFlagValue BoolFalse = Response->ResolveValue(TEXT("bool-false"));
	TestTrue(TEXT("bool-false is bool"), BoolFalse.IsBool());
	TestFalse(TEXT("bool-false disabled"), BoolFalse.IsEnabled());

	const FPostHogFlagValue Variant = Response->ResolveValue(TEXT("variant"));
	TestTrue(TEXT("variant is string"), Variant.IsString());
	TestTrue(TEXT("variant enabled"), Variant.IsEnabled());
	TestEqual(TEXT("variant value"), Variant.StringValue, FString(TEXT("control")));

	const FPostHogFlagValue EmptyVariant = Response->ResolveValue(TEXT("empty-variant"));
	TestTrue(TEXT("empty-variant is string"), EmptyVariant.IsString());
	TestTrue(TEXT("empty-variant has value"), EmptyVariant.HasValue());
	TestFalse(TEXT("empty-variant not enabled"), EmptyVariant.IsEnabled());

	const FPostHogFlagValue Missing = Response->ResolveValue(TEXT("does-not-exist"));
	TestFalse(TEXT("missing has no value"), Missing.HasValue());
	TestTrue(TEXT("missing type"), Missing.Type == EPostHogFlagValueType::Missing);
	TestFalse(TEXT("missing not enabled"), Missing.IsEnabled());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFlagVariantPrecedenceTest, "UnrealHog.FeatureFlags.Models.VariantOverEnabled", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFlagVariantPrecedenceTest::RunTest(const FString& Parameters)
{
	// v4 flag with both enabled and a variant: the variant must win.
	const FString VariantJson = TEXT(R"({
		"flags": {
			"multi": { "enabled": true, "variant": "test-b" }
		}
	})");

	const TOptional<FPostHogFeatureFlagsResponse> WithVariant = FPostHogFeatureFlagsResponse::FromJson(ParseObject(VariantJson));
	TestTrue(TEXT("with-variant parsed"), WithVariant.IsSet());
	const FPostHogFlagValue VariantValue = WithVariant->ResolveValue(TEXT("multi"));
	TestTrue(TEXT("variant wins over enabled"), VariantValue.IsString());
	TestEqual(TEXT("variant value"), VariantValue.StringValue, FString(TEXT("test-b")));

	// v4 flag with enabled only: resolves to boolean.
	const FString EnabledJson = TEXT(R"({
		"flags": {
			"boolflag": { "enabled": true }
		}
	})");

	const TOptional<FPostHogFeatureFlagsResponse> EnabledOnly = FPostHogFeatureFlagsResponse::FromJson(ParseObject(EnabledJson));
	TestTrue(TEXT("enabled-only parsed"), EnabledOnly.IsSet());
	const FPostHogFlagValue EnabledValue = EnabledOnly->ResolveValue(TEXT("boolflag"));
	TestTrue(TEXT("enabled-only is bool"), EnabledValue.IsBool());
	TestTrue(TEXT("enabled-only enabled"), EnabledValue.IsEnabled());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFlagV4PrecedenceTest, "UnrealHog.FeatureFlags.Models.V4OverLegacy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFlagV4PrecedenceTest::RunTest(const FString& Parameters)
{
	// Same key present in both legacy featureFlags and v4 flags; v4 must take precedence. A
	// legacy-only key must still resolve.
	const FString Json = TEXT(R"({
		"featureFlags": {
			"shared": "legacy-variant",
			"legacy-only": true
		},
		"flags": {
			"shared": { "enabled": true, "variant": "v4-variant" }
		}
	})");

	const TOptional<FPostHogFeatureFlagsResponse> Response = FPostHogFeatureFlagsResponse::FromJson(ParseObject(Json));
	TestTrue(TEXT("response parsed"), Response.IsSet());

	const FPostHogFlagValue Shared = Response->ResolveValue(TEXT("shared"));
	TestTrue(TEXT("shared resolves to v4 string"), Shared.IsString());
	TestEqual(TEXT("shared uses v4 variant"), Shared.StringValue, FString(TEXT("v4-variant")));

	const FPostHogFlagValue LegacyOnly = Response->ResolveValue(TEXT("legacy-only"));
	TestTrue(TEXT("legacy-only resolves"), LegacyOnly.IsBool());
	TestTrue(TEXT("legacy-only enabled"), LegacyOnly.IsEnabled());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFlagPayloadLosslessTest, "UnrealHog.FeatureFlags.Models.NestedPayloadLossless", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFlagPayloadLosslessTest::RunTest(const FString& Parameters)
{
	// v4 metadata payload with nested structure must survive parse + round trip verbatim, and v4
	// payload precedence must win over a legacy payload for the same key.
	const FString Json = TEXT(R"({
		"featureFlagPayloads": {
			"cfg": { "legacy": true }
		},
		"flags": {
			"cfg": {
				"enabled": true,
				"metadata": {
					"id": 42,
					"version": 3,
					"payload": { "nested": { "arr": [1, 2, 3], "flag": true, "name": "x" } }
				}
			}
		}
	})");

	const TOptional<FPostHogFeatureFlagsResponse> Response = FPostHogFeatureFlagsResponse::FromJson(ParseObject(Json));
	TestTrue(TEXT("response parsed"), Response.IsSet());

	const TSharedPtr<FJsonValue> Payload = Response->ResolvePayload(TEXT("cfg"));
	TestTrue(TEXT("payload resolved from v4"), Payload.IsValid() && Payload->Type == EJson::Object);

	// The v4 payload (object) must win over the legacy payload; assert its nested contents survived.
	const TSharedPtr<FJsonObject> PayloadObject = Payload->AsObject();
	const TSharedPtr<FJsonObject>* NestedObject = nullptr;
	TestTrue(TEXT("payload has nested object"), PayloadObject->TryGetObjectField(TEXT("nested"), NestedObject));

	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	TestTrue(TEXT("nested array present"), (*NestedObject)->TryGetArrayField(TEXT("arr"), Arr));
	TestEqual(TEXT("nested array length"), Arr->Num(), 3);

	// Round trip: serialize the model, reparse, and confirm the nested payload is byte-identical.
	const FString FirstJson = SerializeObject(Response->ToJson());
	const TOptional<FPostHogFeatureFlagsResponse> Reparsed = FPostHogFeatureFlagsResponse::FromJson(ParseObject(FirstJson));
	TestTrue(TEXT("reparsed"), Reparsed.IsSet());

	const TSharedPtr<FJsonValue> ReparsedPayload = Reparsed->ResolvePayload(TEXT("cfg"));
	TestTrue(TEXT("reparsed payload valid"), ReparsedPayload.IsValid());

	const TSharedRef<FJsonObject> PayloadWrapperA = MakeShared<FJsonObject>();
	PayloadWrapperA->SetField(TEXT("p"), Payload);
	const TSharedRef<FJsonObject> PayloadWrapperB = MakeShared<FJsonObject>();
	PayloadWrapperB->SetField(TEXT("p"), ReparsedPayload);
	TestEqual(TEXT("payload survives round trip"), SerializeObject(PayloadWrapperB), SerializeObject(PayloadWrapperA));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFlagUnknownFieldsTest, "UnrealHog.FeatureFlags.Models.UnknownFieldsTolerated", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFlagUnknownFieldsTest::RunTest(const FString& Parameters)
{
	// Unknown top-level fields and unknown per-flag fields must not fail the parse.
	const FString Json = TEXT(R"({
		"unknownTopLevel": { "whatever": 1 },
		"anotherUnknown": [1, 2],
		"featureFlags": { "a": true },
		"flags": {
			"b": { "enabled": true, "someFutureField": "ignored", "nested": { "x": 1 } }
		}
	})");

	const TOptional<FPostHogFeatureFlagsResponse> Response = FPostHogFeatureFlagsResponse::FromJson(ParseObject(Json));
	TestTrue(TEXT("parse succeeds with unknown fields"), Response.IsSet());
	TestTrue(TEXT("known legacy flag resolves"), Response->ResolveValue(TEXT("a")).IsEnabled());
	TestTrue(TEXT("known v4 flag resolves"), Response->ResolveValue(TEXT("b")).IsEnabled());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFlagMalformedFailSafeTest, "UnrealHog.FeatureFlags.Models.MalformedRootFailsSafe", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFlagMalformedFailSafeTest::RunTest(const FString& Parameters)
{
	// A null/invalid root object must return an unset optional so a valid cache is never replaced.
	const TOptional<FPostHogFeatureFlagsResponse> FromNull = FPostHogFeatureFlagsResponse::FromJson(nullptr);
	TestFalse(TEXT("null root returns unset"), FromNull.IsSet());

	// A non-object JSON root fails to deserialize into an object; ParseObject yields null.
	TestFalse(TEXT("array root not an object"), ParseObject(TEXT("[1, 2, 3]")).IsValid());

	// Flag FromJson of an invalid object is unset.
	const TOptional<FPostHogFeatureFlag> FlagFromNull = FPostHogFeatureFlag::FromJson(nullptr);
	TestFalse(TEXT("flag from null unset"), FlagFromNull.IsSet());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFlagRoundTripMetadataTest, "UnrealHog.FeatureFlags.Models.RoundTripPreservesMetadata", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFlagRoundTripMetadataTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"errorsWhileComputingFlags": true,
		"requestId": "req-123",
		"evaluatedAt": 1718000000000,
		"quotaLimited": ["k1", "k2"],
		"featureFlags": { "legacy": "variant-a" },
		"featureFlagPayloads": { "legacy": { "p": 1 } },
		"flags": {
			"f": {
				"enabled": true,
				"variant": "v",
				"metadata": { "id": 7, "version": 9, "payload": { "deep": [true, "s"] } },
				"reason": { "description": "matched condition set 1" }
			}
		}
	})");

	const TOptional<FPostHogFeatureFlagsResponse> First = FPostHogFeatureFlagsResponse::FromJson(ParseObject(Json));
	TestTrue(TEXT("first parse"), First.IsSet());

	const TSharedRef<FJsonObject> Serialized = First->ToJson();

	// ToJson always writes _version == CurrentVersion.
	double VersionOut = 0.0;
	TestTrue(TEXT("has _version"), Serialized->TryGetNumberField(TEXT("_version"), VersionOut));
	TestEqual(TEXT("_version is 2"), static_cast<int32>(VersionOut), FPostHogFeatureFlagsResponse::CurrentVersion);

	const FString RoundTripJson = SerializeObject(Serialized);
	const TOptional<FPostHogFeatureFlagsResponse> Second = FPostHogFeatureFlagsResponse::FromJson(ParseObject(RoundTripJson));
	TestTrue(TEXT("second parse"), Second.IsSet());

	TestTrue(TEXT("errorsWhileComputingFlags preserved"), Second->bErrorsWhileComputingFlags);
	TestTrue(TEXT("requestId preserved"), Second->RequestId.IsSet());
	TestEqual(TEXT("requestId value"), Second->RequestId.GetValue(), FString(TEXT("req-123")));
	TestTrue(TEXT("evaluatedAt preserved"), Second->EvaluatedAt.IsSet());
	TestEqual(TEXT("evaluatedAt value"), Second->EvaluatedAt.GetValue(), static_cast<int64>(1718000000000LL));
	TestEqual(TEXT("quotaLimited count"), Second->QuotaLimited.Num(), 2);
	TestTrue(TEXT("quotaLimited k1"), Second->QuotaLimited.Contains(TEXT("k1")));
	TestTrue(TEXT("quotaLimited k2"), Second->QuotaLimited.Contains(TEXT("k2")));

	const FPostHogFeatureFlag* Flag = Second->Flags.Find(TEXT("f"));
	TestTrue(TEXT("flag present"), Flag != nullptr);
	if (Flag != nullptr)
	{
		TestTrue(TEXT("metadata preserved"), Flag->Metadata.IsSet());
		TestEqual(TEXT("metadata id"), Flag->Metadata->Id, 7);
		TestEqual(TEXT("metadata version"), Flag->Metadata->Version, 9);
		TestTrue(TEXT("metadata payload preserved"), Flag->Metadata->Payload.IsValid());
		TestTrue(TEXT("reason preserved"), Flag->Reason.IsSet());
		TestEqual(TEXT("reason description"), Flag->Reason->Description, FString(TEXT("matched condition set 1")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogFlagToJsonOmitsAbsentTest, "UnrealHog.FeatureFlags.Models.ToJsonOmitsAbsentOptionals", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogFlagToJsonOmitsAbsentTest::RunTest(const FString& Parameters)
{
	// A minimal response should serialize only _version + errorsWhileComputingFlags.
	const TOptional<FPostHogFeatureFlagsResponse> Response = FPostHogFeatureFlagsResponse::FromJson(ParseObject(TEXT("{}")));
	TestTrue(TEXT("empty object parses"), Response.IsSet());

	const TSharedRef<FJsonObject> Serialized = Response->ToJson();
	TestTrue(TEXT("has _version"), Serialized->HasField(TEXT("_version")));
	TestTrue(TEXT("has errorsWhileComputingFlags"), Serialized->HasField(TEXT("errorsWhileComputingFlags")));
	TestFalse(TEXT("no featureFlags"), Serialized->HasField(TEXT("featureFlags")));
	TestFalse(TEXT("no featureFlagPayloads"), Serialized->HasField(TEXT("featureFlagPayloads")));
	TestFalse(TEXT("no flags"), Serialized->HasField(TEXT("flags")));
	TestFalse(TEXT("no quotaLimited"), Serialized->HasField(TEXT("quotaLimited")));
	TestFalse(TEXT("no requestId"), Serialized->HasField(TEXT("requestId")));
	TestFalse(TEXT("no evaluatedAt"), Serialized->HasField(TEXT("evaluatedAt")));

	// A flag with only enabled must omit variant/metadata/reason.
	const TOptional<FPostHogFeatureFlag> Flag = FPostHogFeatureFlag::FromJson(ParseObject(TEXT("{ \"enabled\": false }")));
	TestTrue(TEXT("flag parses"), Flag.IsSet());
	const TSharedRef<FJsonObject> FlagJson = Flag->ToJson();
	TestTrue(TEXT("flag has enabled"), FlagJson->HasField(TEXT("enabled")));
	TestFalse(TEXT("flag omits variant"), FlagJson->HasField(TEXT("variant")));
	TestFalse(TEXT("flag omits metadata"), FlagJson->HasField(TEXT("metadata")));
	TestFalse(TEXT("flag omits reason"), FlagJson->HasField(TEXT("reason")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
