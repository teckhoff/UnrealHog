// Trevor Eckhoff, 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * 
 */
class PostHogSdkInfo
{
public:
	static FString GetLibraryName();
	static FString GetPluginVersion();
	static FString GetUserAgent();
	
private:
	PostHogSdkInfo() = delete;
};
