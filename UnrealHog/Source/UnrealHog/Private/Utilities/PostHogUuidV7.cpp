// Trevor Eckhoff, 2026. All rights reserved.

#include "Utilities/PostHogUuidV7.h"

#include "Misc/DateTime.h"
#include "Misc/Timespan.h"


namespace PostHogUuidV7
{
	FString Pack(uint64 TimestampMs, uint16 RandA12, uint64 RandB62)
	{
		const uint32 A = static_cast<uint32>(TimestampMs >> 16);
		const uint32 B = (static_cast<uint32>(TimestampMs & 0xFFFFULL) << 16)
			| (0x7u << 12)
			| (static_cast<uint32>(RandA12) & 0xFFFu);
		const uint32 C = (0x2u << 30) | static_cast<uint32>((RandB62 >> 32) & 0x3FFFFFFFULL);
		const uint32 D = static_cast<uint32>(RandB62 & 0xFFFFFFFFULL);

		const FGuid Guid(A, B, C, D);

		return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
	}

	FString FGenerator::Generate(TFunctionRef<uint64()> ClockSourceMs, TFunctionRef<FGuid()> EntropySource)
	{
		uint64 TimestampMs;
		uint16 RandA;

		{
			FScopeLock ScopeLock(&Lock);

			const uint64 WallClockMs = ClockSourceMs();

			if (!bHasGeneratedValue || WallClockMs > LastTimestampMilliseconds)
			{
				LastTimestampMilliseconds = WallClockMs;
				Counter = 0;
				bHasGeneratedValue = true;
			}
			else
			{
				++Counter;

				if (Counter > 0xFFFu)
				{
					++LastTimestampMilliseconds;
					Counter = 0;
				}
			}

			if (LastTimestampMilliseconds > 0xFFFFFFFFFFFFULL)
			{
				ensureMsgf(false, TEXT("PostHogUuidV7: timestamp exceeds 48-bit range"));
				return FString();
			}

			TimestampMs = LastTimestampMilliseconds;
			RandA = Counter;
		}

		FGuid Entropy = EntropySource();

		if (!Entropy.IsValid())
		{
			Entropy = EntropySource();

			if (!Entropy.IsValid())
			{
				ensureMsgf(false, TEXT("PostHogUuidV7: entropy source produced invalid GUID"));
				return FString();
			}
		}

		const uint64 RandB62 = ((static_cast<uint64>(Entropy.C) << 32) | Entropy.D) & 0x3FFFFFFFFFFFFFFFULL;

		return Pack(TimestampMs, RandA, RandB62);
	}

	FString New()
	{
		static FGenerator ProductionGenerator;

		return ProductionGenerator.Generate(
			[]() -> uint64
			{
				const int64 TicksSinceEpoch = FDateTime::UtcNow().GetTicks() - FDateTime(1970, 1, 1).GetTicks();
				return static_cast<uint64>(TicksSinceEpoch / ETimespan::TicksPerMillisecond);
			},
			[]() -> FGuid
			{
				return FGuid::NewGuid();
			});
	}
}
