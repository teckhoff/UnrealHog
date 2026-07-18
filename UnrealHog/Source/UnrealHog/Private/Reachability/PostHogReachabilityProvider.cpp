#include "Reachability/PostHogReachabilityProvider.h"

#include "HAL/PlatformMisc.h"

EPostHogReachabilityState FPostHogPlatformReachabilityProvider::GetReachability() const
{
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
