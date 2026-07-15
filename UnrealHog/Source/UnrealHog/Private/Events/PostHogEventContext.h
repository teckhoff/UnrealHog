#pragma once

#include "CoreMinimal.h"
#include "Misc/Optional.h"

// A PostHog device/form-factor classification, independent of $platform/$platform_variant.
enum class EPostHogDeviceFormFactor : uint8
{
	Unknown,
	Desktop,
	Mobile,
	Web,
	Console,
	Tv,
	Xr,
	Server
};

// Independently sourced platform/app/screen values used to populate SDK-owned event properties.
// Captured once per event via FPostHogEventContextProvider::Capture() so that the serialization
// logic in FPostHogEvent can remain a pure function of this value type for deterministic tests.
struct FPostHogEventContext
{
	FString PlatformName;
	FString PlatformVariant;

	// Raw OS label as reported by the platform (e.g. FPlatformMisc::GetOSVersions()); normalized
	// separately for $os via PostHogEventContextNormalization::NormalizeOsName.
	FString OsLabel;

	// Existing $os_version source; independent of OsLabel above.
	FString OsVersion;

	EPostHogDeviceFormFactor DeviceFormFactor = EPostHogDeviceFormFactor::Unknown;

	// Independent of DeviceModel; never sourced from FPlatformMisc::GetDeviceMakeAndModel().
	FString DeviceManufacturer;

	FString DeviceModel;

	FString AppName;
	FString AppVersion;

	// Independent of AppVersion; never sourced from UGeneralProjectSettings::ProjectVersion or FApp::GetBuildVersion().
	FString AppBuild;

	TOptional<double> ScreenWidth;
	TOptional<double> ScreenHeight;
};

// Pure normalization/mapping functions over FPostHogEventContext fields. No global/platform access.
namespace PostHogEventContextNormalization
{
	// Normalizes a raw OS label to a stable PostHog category (Windows, macOS, Linux, Android, iOS,
	// tvOS, visionOS); returns an empty string for an unrecognized label.
	FString NormalizeOsName(const FString& RawOsLabel);

	// Maps a device form factor to a stable PostHog device-type category (Mobile, Desktop, Web);
	// returns an empty string for categories without an intentional PostHog mapping.
	FString MapDeviceType(EPostHogDeviceFormFactor FormFactor);
}

// Captures the current platform/app/screen state into an FPostHogEventContext. The only place
// that reads global platform state for event enrichment; all other logic operates on the captured value.
class FPostHogEventContextProvider
{
public:
	static FPostHogEventContext Capture();

private:
	FPostHogEventContextProvider() = delete;
};
