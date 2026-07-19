#include "Reachability/PostHogReachabilityProvider.h"

#include "HAL/PlatformMisc.h"

EPostHogReachabilityState FPostHogPlatformReachabilityProvider::GetReachability() const
{
	// GetNetworkConnectionStatus defaults to Connected on platforms that never update it, so it is
	// only trustworthy as a negative signal; positive/unknown states fall through to the
	// connection-type check below.
	switch (FPlatformMisc::GetNetworkConnectionStatus())
	{
	case ENetworkConnectionStatus::Disabled:
	case ENetworkConnectionStatus::Local:
		return EPostHogReachabilityState::NotReachable;
	default:
		break;
	}

	switch (FPlatformMisc::GetNetworkConnectionType())
	{
	case ENetworkConnectionType::None:
	case ENetworkConnectionType::AirplaneMode:
		return EPostHogReachabilityState::NotReachable;
	case ENetworkConnectionType::Cell:
	case ENetworkConnectionType::WiFi:
	case ENetworkConnectionType::WiMAX:
	case ENetworkConnectionType::Bluetooth:
	case ENetworkConnectionType::Ethernet:
		return EPostHogReachabilityState::Reachable;
	case ENetworkConnectionType::Unknown:
	default:
		return EPostHogReachabilityState::Unknown;
	}
}
