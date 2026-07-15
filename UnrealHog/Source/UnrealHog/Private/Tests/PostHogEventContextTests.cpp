#include "Events/PostHogEventContext.h"
#include "Events/PostHogEvent.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventContextNormalizesOsNameTest, "UnrealHog.Events.EventContext.NormalizesOsName", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventContextNormalizesOsNameTest::RunTest(const FString& Parameters)
{
	using namespace PostHogEventContextNormalization;

	TestEqual(TEXT("Windows label normalizes"), NormalizeOsName(TEXT("Windows 11")), TEXT("Windows"));
	TestEqual(TEXT("macOS label normalizes"), NormalizeOsName(TEXT("macOS 14.1")), TEXT("macOS"));
	TestEqual(TEXT("OS X label normalizes"), NormalizeOsName(TEXT("OS X 10.15")), TEXT("macOS"));
	TestEqual(TEXT("Linux label normalizes"), NormalizeOsName(TEXT("Linux")), TEXT("Linux"));
	TestEqual(TEXT("Android label normalizes"), NormalizeOsName(TEXT("Android 14")), TEXT("Android"));
	TestEqual(TEXT("iOS label normalizes"), NormalizeOsName(TEXT("iOS 17")), TEXT("iOS"));
	TestEqual(TEXT("tvOS label normalizes"), NormalizeOsName(TEXT("tvOS 17")), TEXT("tvOS"));
	TestEqual(TEXT("visionOS label normalizes"), NormalizeOsName(TEXT("visionOS 1")), TEXT("visionOS"));
	TestEqual(TEXT("Unrecognized label omitted"), NormalizeOsName(TEXT("AmigaOS 4")), TEXT(""));
	TestEqual(TEXT("Empty label omitted"), NormalizeOsName(TEXT("")), TEXT(""));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventContextMapsDeviceTypeTest, "UnrealHog.Events.EventContext.MapsDeviceType", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventContextMapsDeviceTypeTest::RunTest(const FString& Parameters)
{
	using namespace PostHogEventContextNormalization;

	TestEqual(TEXT("Mobile maps"), MapDeviceType(EPostHogDeviceFormFactor::Mobile), TEXT("Mobile"));
	TestEqual(TEXT("Desktop maps"), MapDeviceType(EPostHogDeviceFormFactor::Desktop), TEXT("Desktop"));
	TestEqual(TEXT("Web maps"), MapDeviceType(EPostHogDeviceFormFactor::Web), TEXT("Web"));
	TestEqual(TEXT("Console omitted"), MapDeviceType(EPostHogDeviceFormFactor::Console), TEXT(""));
	TestEqual(TEXT("Tv omitted"), MapDeviceType(EPostHogDeviceFormFactor::Tv), TEXT(""));
	TestEqual(TEXT("Xr omitted"), MapDeviceType(EPostHogDeviceFormFactor::Xr), TEXT(""));
	TestEqual(TEXT("Server omitted"), MapDeviceType(EPostHogDeviceFormFactor::Server), TEXT(""));
	TestEqual(TEXT("Unknown omitted"), MapDeviceType(EPostHogDeviceFormFactor::Unknown), TEXT(""));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventContextSerializesWithoutAliasingTest, "UnrealHog.Events.EventContext.SerializesWithoutAliasingExistingFields", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventContextSerializesWithoutAliasingTest::RunTest(const FString& Parameters)
{
	FPostHogEventContext Context;
	Context.PlatformName = TEXT("WindowsFake");
	Context.PlatformVariant = TEXT("FakeVariant");
	Context.OsLabel = TEXT("Android 14");
	Context.OsVersion = TEXT("11.0.1");
	Context.DeviceFormFactor = EPostHogDeviceFormFactor::Mobile;
	Context.DeviceManufacturer = TEXT("Acme");
	Context.DeviceModel = TEXT("AcmeModel-9");
	Context.AppName = TEXT("FakeApp");
	Context.AppVersion = TEXT("1.2.3");
	Context.AppBuild = TEXT("456");
	Context.ScreenWidth = 1920.0;
	Context.ScreenHeight = 1080.0;

	FPostHogEvent Event(TEXT("test-event"), TEXT("distinct-1"));
	Event.ApplySdkProperties(false, Context);

	const TSharedRef<FJsonObject> JsonObject = Event.ToJsonObject();
	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("JSON has properties object"), JsonObject->TryGetObjectField(TEXT("properties"), PropertiesObject));

	FString OsValue;
	TestTrue(TEXT("$os present"), (*PropertiesObject)->TryGetStringField(TEXT("$os"), OsValue));
	TestEqual(TEXT("$os normalized from OsLabel"), OsValue, TEXT("Android"));

	FString DeviceTypeValue;
	TestTrue(TEXT("$device_type present"), (*PropertiesObject)->TryGetStringField(TEXT("$device_type"), DeviceTypeValue));
	TestEqual(TEXT("$device_type mapped from DeviceFormFactor"), DeviceTypeValue, TEXT("Mobile"));

	FString DeviceManufacturerValue;
	TestTrue(TEXT("$device_manufacturer present"), (*PropertiesObject)->TryGetStringField(TEXT("$device_manufacturer"), DeviceManufacturerValue));
	TestEqual(TEXT("$device_manufacturer matches Context"), DeviceManufacturerValue, TEXT("Acme"));

	FString AppBuildValue;
	TestTrue(TEXT("$app_build present"), (*PropertiesObject)->TryGetStringField(TEXT("$app_build"), AppBuildValue));
	TestEqual(TEXT("$app_build matches Context"), AppBuildValue, TEXT("456"));

	FString PlatformValue;
	TestTrue(TEXT("$platform present"), (*PropertiesObject)->TryGetStringField(TEXT("$platform"), PlatformValue));
	TestNotEqual(TEXT("$os does not alias $platform"), OsValue, PlatformValue);

	FString OsVersionValue;
	TestTrue(TEXT("$os_version present"), (*PropertiesObject)->TryGetStringField(TEXT("$os_version"), OsVersionValue));
	TestNotEqual(TEXT("$os does not alias $os_version"), OsValue, OsVersionValue);

	FString DeviceModelValue;
	TestTrue(TEXT("$device_model present"), (*PropertiesObject)->TryGetStringField(TEXT("$device_model"), DeviceModelValue));
	TestNotEqual(TEXT("$device_manufacturer does not alias $device_model"), DeviceManufacturerValue, DeviceModelValue);

	FString AppVersionValue;
	TestTrue(TEXT("$app_version present"), (*PropertiesObject)->TryGetStringField(TEXT("$app_version"), AppVersionValue));
	TestNotEqual(TEXT("$app_build does not alias $app_version"), AppBuildValue, AppVersionValue);

	FString PlatformVariantValue;
	TestTrue(TEXT("$platform_variant present"), (*PropertiesObject)->TryGetStringField(TEXT("$platform_variant"), PlatformVariantValue));
	TestNotEqual(TEXT("$device_type does not alias $platform_variant"), DeviceTypeValue, PlatformVariantValue);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventContextOmitsUnavailableNewPropertiesTest, "UnrealHog.Events.EventContext.OmitsUnavailableNewProperties", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventContextOmitsUnavailableNewPropertiesTest::RunTest(const FString& Parameters)
{
	FPostHogEventContext Context;
	Context.PlatformName = TEXT("WindowsFake");
	Context.OsLabel = TEXT("");
	Context.DeviceFormFactor = EPostHogDeviceFormFactor::Unknown;
	Context.DeviceManufacturer = TEXT("");
	Context.AppBuild = TEXT("");
	Context.AppName = TEXT("FakeApp");
	Context.AppVersion = TEXT("1.0.0");

	FPostHogEvent Event(TEXT("test-event"), TEXT("distinct-1"));
	Event.ApplySdkProperties(false, Context);

	const TSharedRef<FJsonObject> JsonObject = Event.ToJsonObject();
	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("JSON has properties object"), JsonObject->TryGetObjectField(TEXT("properties"), PropertiesObject));

	TestFalse(TEXT("$os omitted"), (*PropertiesObject)->HasField(TEXT("$os")));
	TestFalse(TEXT("$device_type omitted"), (*PropertiesObject)->HasField(TEXT("$device_type")));
	TestFalse(TEXT("$device_manufacturer omitted"), (*PropertiesObject)->HasField(TEXT("$device_manufacturer")));
	TestFalse(TEXT("$app_build omitted"), (*PropertiesObject)->HasField(TEXT("$app_build")));

	TestTrue(TEXT("$platform still present"), (*PropertiesObject)->HasField(TEXT("$platform")));
	TestTrue(TEXT("$app_name still present"), (*PropertiesObject)->HasField(TEXT("$app_name")));
	TestTrue(TEXT("$app_version still present"), (*PropertiesObject)->HasField(TEXT("$app_version")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogEventContextMobileManufacturerAndBuildTest, "UnrealHog.Events.EventContext.SerializesMobileManufacturerAndBuild", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogEventContextMobileManufacturerAndBuildTest::RunTest(const FString& Parameters)
{
	FPostHogEventContext Context;
	Context.PlatformName = TEXT("AndroidFake");
	Context.OsLabel = TEXT("Android 14");
	Context.DeviceFormFactor = EPostHogDeviceFormFactor::Mobile;
	Context.DeviceManufacturer = TEXT("Google");
	Context.DeviceModel = TEXT("Pixel Fake");
	Context.AppName = TEXT("FakeApp");
	Context.AppVersion = TEXT("2.0.0");
	Context.AppBuild = TEXT("789");

	FPostHogEvent Event(TEXT("test-event"), TEXT("distinct-1"));
	Event.ApplySdkProperties(false, Context);

	const TSharedRef<FJsonObject> JsonObject = Event.ToJsonObject();
	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("JSON has properties object"), JsonObject->TryGetObjectField(TEXT("properties"), PropertiesObject));

	FString DeviceManufacturerValue;
	TestTrue(TEXT("$device_manufacturer present"), (*PropertiesObject)->TryGetStringField(TEXT("$device_manufacturer"), DeviceManufacturerValue));
	TestEqual(TEXT("$device_manufacturer matches Android-like value"), DeviceManufacturerValue, TEXT("Google"));

	FString AppBuildValue;
	TestTrue(TEXT("$app_build present"), (*PropertiesObject)->TryGetStringField(TEXT("$app_build"), AppBuildValue));
	TestEqual(TEXT("$app_build matches Android/iOS-like value"), AppBuildValue, TEXT("789"));

	FString DeviceTypeValue;
	TestTrue(TEXT("$device_type present"), (*PropertiesObject)->TryGetStringField(TEXT("$device_type"), DeviceTypeValue));
	TestEqual(TEXT("$device_type is Mobile"), DeviceTypeValue, TEXT("Mobile"));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
