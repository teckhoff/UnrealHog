// Trevor Eckhoff, 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"
#include "HAL/CriticalSection.h"
#include "Templates/Function.h"

/**
 * @brief Private RFC 9562 UUIDv7 generation utility.
 *
 * Produces platform-independent, lowercase hyphenated UUIDv7 strings regardless of the
 * target platform's FGuid::NewGuid() version. Not exposed outside the runtime module.
 */
namespace PostHogUuidV7
{
	// Production entry point. Lazily initializes a process-local generator; performs no work
	// at static/module init time so it never violates the opt-in analytics collection boundary.
	FString New();

	// Pure, deterministic packer used by both New() and automation tests.
	FString Pack(uint64 TimestampMs, uint16 RandA12, uint64 RandB62);

	class FGenerator
	{
	public:
		FString Generate(TFunctionRef<uint64()> ClockSourceMs, TFunctionRef<FGuid()> EntropySource);

	private:
		FCriticalSection Lock;
		bool bHasGeneratedValue = false;
		uint64 LastTimestampMilliseconds = 0;
		uint16 Counter = 0;
	};
}
