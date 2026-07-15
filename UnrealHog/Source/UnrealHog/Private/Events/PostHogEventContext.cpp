#include "Events/PostHogEventContext.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GeneralProjectSettings.h"

#if PLATFORM_ANDROID
#include "Android/AndroidPlatformMisc.h"
#elif PLATFORM_IOS || PLATFORM_TVOS || PLATFORM_VISIONOS
#include "IOS/IOSPlatformMisc.h"
#endif

FString PostHogEventContextNormalization::NormalizeOsName(const FString& RawOsLabel)
{
	if (RawOsLabel.Contains(TEXT("Windows")))
	{
		return TEXT("Windows");
	}

	if (RawOsLabel.Contains(TEXT("visionOS")))
	{
		return TEXT("visionOS");
	}

	if (RawOsLabel.Contains(TEXT("tvOS")))
	{
		return TEXT("tvOS");
	}

	if (RawOsLabel.Contains(TEXT("iOS")))
	{
		return TEXT("iOS");
	}

	if (RawOsLabel.Contains(TEXT("Android")))
	{
		return TEXT("Android");
	}

	if (RawOsLabel.Contains(TEXT("Mac")) || RawOsLabel.Contains(TEXT("OS X")))
	{
		return TEXT("macOS");
	}

	if (RawOsLabel.Contains(TEXT("Linux")))
	{
		return TEXT("Linux");
	}

	return FString();
}

FString PostHogEventContextNormalization::MapDeviceType(EPostHogDeviceFormFactor FormFactor)
{
	switch (FormFactor)
	{
	case EPostHogDeviceFormFactor::Mobile:
		return TEXT("Mobile");
	case EPostHogDeviceFormFactor::Desktop:
		return TEXT("Desktop");
	case EPostHogDeviceFormFactor::Web:
		return TEXT("Web");
	default:
		return FString();
	}
}

FPostHogEventContext FPostHogEventContextProvider::Capture()
{
	FPostHogEventContext Context;

	Context.PlatformName = FPlatformProperties::PlatformName();
	Context.PlatformVariant = FPlatformProperties::PlatformVariantName();

	FString OsSubVersionLabel;
	FPlatformMisc::GetOSVersions(Context.OsLabel, OsSubVersionLabel);

	Context.OsVersion = FPlatformMisc::GetOSVersion();
	Context.DeviceModel = FPlatformMisc::GetDeviceMakeAndModel();

#if PLATFORM_ANDROID
	Context.DeviceManufacturer = FAndroidMisc::GetDeviceMake();
	Context.DeviceFormFactor = EPostHogDeviceFormFactor::Mobile;
#elif PLATFORM_IOS || PLATFORM_TVOS || PLATFORM_VISIONOS
	Context.DeviceManufacturer = TEXT("Apple");
	Context.AppBuild = FIOSPlatformMisc::GetBuildNumber();
	Context.DeviceFormFactor = EPostHogDeviceFormFactor::Mobile;
#elif PLATFORM_WINDOWS || PLATFORM_MAC || PLATFORM_LINUX
	Context.DeviceFormFactor = EPostHogDeviceFormFactor::Desktop;
#endif

	const UGeneralProjectSettings* ProjectSettings = GetDefault<UGeneralProjectSettings>();
	Context.AppName = ProjectSettings->ProjectName;
	Context.AppVersion = ProjectSettings->ProjectVersion;

	if (GEngine && GEngine->GameViewport)
	{
		FVector2D ViewportSize;
		GEngine->GameViewport->GetViewportSize(ViewportSize);
		Context.ScreenWidth = ViewportSize.X;
		Context.ScreenHeight = ViewportSize.Y;
	}

	return Context;
}
