
#include "SDK/PostHogSdkInfo.h"

#include "Interfaces/IPluginManager.h"

namespace
{
	constexpr const TCHAR* PluginDescriptorName = TEXT("UnrealHog");
	constexpr const TCHAR* FallbackPluginVersion = TEXT("1.0.0");	
}

FString PostHogSdkInfo::GetLibraryName()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginDescriptorName);
	
	if (Plugin.IsValid() && !Plugin->GetDescriptor().FriendlyName.IsEmpty())
	{
		return Plugin->GetDescriptor().FriendlyName	;
	}
	
	return PluginDescriptorName;
}

FString PostHogSdkInfo::GetPluginVersion()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginDescriptorName);
	
	if (Plugin.IsValid() && !Plugin->GetDescriptor().VersionName.IsEmpty())
	{
		return Plugin->GetDescriptor().VersionName;
	}
	
	return FallbackPluginVersion;
}

FString PostHogSdkInfo::GetUserAgent()
{
	return FString::Printf(TEXT("%s/%s"), *GetLibraryName(), *GetPluginVersion());
}
