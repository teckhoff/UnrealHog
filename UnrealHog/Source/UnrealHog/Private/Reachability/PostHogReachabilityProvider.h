#pragma once

#include "CoreMinimal.h"

enum class EPostHogReachabilityState : uint8
{
	Unknown,
	Reachable,
	NotReachable
};

class IPostHogReachabilityProvider
{
public:
	virtual ~IPostHogReachabilityProvider() = default;
	virtual EPostHogReachabilityState GetReachability() const = 0;
};

class FPostHogPlatformReachabilityProvider final : public IPostHogReachabilityProvider
{
public:
	virtual EPostHogReachabilityState GetReachability() const override;
};
