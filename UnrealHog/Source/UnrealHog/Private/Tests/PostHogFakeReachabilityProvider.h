#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Reachability/PostHogReachabilityProvider.h"

class FPostHogFakeReachabilityProvider final : public IPostHogReachabilityProvider
{
public:
	explicit FPostHogFakeReachabilityProvider(EPostHogReachabilityState InState = EPostHogReachabilityState::Unknown)
		: State(InState)
	{
	}

	void SetState(EPostHogReachabilityState InState) { State = InState; }

	virtual EPostHogReachabilityState GetReachability() const override { return State; }

private:
	EPostHogReachabilityState State;
};

#endif // WITH_DEV_AUTOMATION_TESTS
