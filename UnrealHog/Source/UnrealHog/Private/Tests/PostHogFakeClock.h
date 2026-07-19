#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Time/PostHogClock.h"

/**
 * @brief Deterministic fake clock for Automation tests, letting retry-backoff tests advance
 * time explicitly instead of sleeping on wall-clock time.
 */
class FPostHogFakeClock final : public IPostHogClock
{
public:
	virtual FDateTime UtcNow() const override { return Current; }

	void Advance(const FTimespan& Delta) { Current += Delta; }

private:
	FDateTime Current = FDateTime(2026, 1, 1, 0, 0, 0);
};

#endif // WITH_DEV_AUTOMATION_TESTS
