#pragma once

#include "CoreMinimal.h"

/**
 * 
 */
class FPostHogSdkInfo
{
public:
	static FString GetLibraryName();
	static FString GetPluginVersion();
	static FString GetUserAgent();
	
private:
	FPostHogSdkInfo() = delete;
};
