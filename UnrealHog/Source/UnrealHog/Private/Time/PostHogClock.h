#pragma once

#include "CoreMinimal.h"

/**
 * Injectable UTC clock seam so retry-backoff timing can be driven deterministically in tests.
 */
class IPostHogClock
{
public:
	virtual ~IPostHogClock() = default;
	virtual FDateTime UtcNow() const = 0;
};

class FPostHogSystemClock final : public IPostHogClock
{
public:
	virtual FDateTime UtcNow() const override { return FDateTime::UtcNow(); }
};
