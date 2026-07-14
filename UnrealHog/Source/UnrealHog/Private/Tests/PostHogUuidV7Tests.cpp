// Trevor Eckhoff, 2026. All rights reserved.

#include "Utilities/PostHogUuidV7.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/DateTime.h"
#include "Misc/Timespan.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/PlatformProcess.h"
#include "HAL/CriticalSection.h"
#include "Containers/Set.h"

namespace
{
	FGuid MakeValidEntropy()
	{
		return FGuid(0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogUuidV7RfcVectorTest, "UnrealHog.Utilities.UuidV7.RfcVector", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogUuidV7RfcVectorTest::RunTest(const FString& Parameters)
{
	const FString Result = PostHogUuidV7::Pack(1645557742000ULL, 0xCC3, 0x18C4DC0C0C07398FULL);

	TestEqual(TEXT("RFC 9562 vector"), Result, TEXT("017f22e2-79b0-7cc3-98c4-dc0c0c07398f"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogUuidV7CanonicalShapeTest, "UnrealHog.Utilities.UuidV7.CanonicalShape", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogUuidV7CanonicalShapeTest::RunTest(const FString& Parameters)
{
	const FString Result = PostHogUuidV7::Pack(1645557742000ULL, 0xCC3, 0x18C4DC0C0C07398FULL);

	TestEqual(TEXT("Length"), Result.Len(), 36);
	TestEqual(TEXT("Hyphen at 8"), Result[8], TCHAR('-'));
	TestEqual(TEXT("Hyphen at 13"), Result[13], TCHAR('-'));
	TestEqual(TEXT("Hyphen at 18"), Result[18], TCHAR('-'));
	TestEqual(TEXT("Hyphen at 23"), Result[23], TCHAR('-'));

	FGuid ParsedGuid;
	const bool bParsed = FGuid::ParseExact(Result, EGuidFormats::DigitsWithHyphensLower, ParsedGuid);
	TestTrue(TEXT("Parses as DigitsWithHyphensLower"), bParsed);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogUuidV7VersionVariantTest, "UnrealHog.Utilities.UuidV7.VersionAndVariant", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogUuidV7VersionVariantTest::RunTest(const FString& Parameters)
{
	const FString Result = PostHogUuidV7::Pack(1645557742000ULL, 0xCC3, 0x18C4DC0C0C07398FULL);

	TestEqual(TEXT("Version nibble"), Result[14], TCHAR('7'));

	const TCHAR VariantChar = Result[19];
	const bool bValidVariant = VariantChar == TCHAR('8') || VariantChar == TCHAR('9') || VariantChar == TCHAR('a') || VariantChar == TCHAR('b');
	TestTrue(TEXT("Variant nibble is 8/9/a/b"), bValidVariant);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogUuidV7TimestampBitsTest, "UnrealHog.Utilities.UuidV7.TimestampBits", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogUuidV7TimestampBitsTest::RunTest(const FString& Parameters)
{
	const uint64 TimestampMs = 0x0000000000ULL;
	const FString ResultZero = PostHogUuidV7::Pack(TimestampMs, 0, 0);
	TestEqual(TEXT("Zero timestamp high 32 bits"), ResultZero.Left(8), TEXT("00000000"));
	TestEqual(TEXT("Zero timestamp low 16 bits"), ResultZero.Mid(9, 4), TEXT("0000"));

	const uint64 MaxTimestampMs = 0xFFFFFFFFFFFFULL;
	const FString ResultMax = PostHogUuidV7::Pack(MaxTimestampMs, 0, 0);
	TestEqual(TEXT("Max timestamp high 32 bits"), ResultMax.Left(8), TEXT("ffffffff"));
	TestEqual(TEXT("Max timestamp low 16 bits"), ResultMax.Mid(9, 4), TEXT("ffff"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogUuidV7SameMillisecondOrderingTest, "UnrealHog.Utilities.UuidV7.SameMillisecondOrdering", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogUuidV7SameMillisecondOrderingTest::RunTest(const FString& Parameters)
{
	PostHogUuidV7::FGenerator Generator;

	const auto FixedClock = []() -> uint64 { return 1000000ULL; };
	const auto Entropy = []() -> FGuid { return MakeValidEntropy(); };

	FString Previous = Generator.Generate(FixedClock, Entropy);
	TestFalse(TEXT("First value non-empty"), Previous.IsEmpty());

	for (int32 Index = 0; Index < 10; ++Index)
	{
		const FString Current = Generator.Generate(FixedClock, Entropy);
		TestFalse(TEXT("Value non-empty"), Current.IsEmpty());
		TestTrue(TEXT("Strictly increasing"), Current > Previous);
		Previous = Current;
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogUuidV7ClockRollbackTest, "UnrealHog.Utilities.UuidV7.ClockRollback", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogUuidV7ClockRollbackTest::RunTest(const FString& Parameters)
{
	PostHogUuidV7::FGenerator Generator;

	const auto Entropy = []() -> FGuid { return MakeValidEntropy(); };

	const FString First = Generator.Generate([]() -> uint64 { return 2000000ULL; }, Entropy);
	const FString Second = Generator.Generate([]() -> uint64 { return 1000000ULL; }, Entropy);

	TestFalse(TEXT("First non-empty"), First.IsEmpty());
	TestFalse(TEXT("Second non-empty"), Second.IsEmpty());
	TestTrue(TEXT("Rollback still increases"), Second > First);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogUuidV7CounterRolloverTest, "UnrealHog.Utilities.UuidV7.CounterRollover", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogUuidV7CounterRolloverTest::RunTest(const FString& Parameters)
{
	PostHogUuidV7::FGenerator Generator;

	const auto FixedClock = []() -> uint64 { return 5000000ULL; };
	const auto Entropy = []() -> FGuid { return MakeValidEntropy(); };

	TSet<FString> Seen;
	FString Previous;

	// Counter starts at 0 on the first call, then increments through 0xFFF (4096 total values)
	// before the 4097th call rolls the logical timestamp forward.
	for (int32 Index = 0; Index < 4097; ++Index)
	{
		const FString Current = Generator.Generate(FixedClock, Entropy);
		TestFalse(TEXT("Value non-empty"), Current.IsEmpty());
		TestFalse(TEXT("No duplicate"), Seen.Contains(Current));
		Seen.Add(Current);

		if (Index > 0)
		{
			TestTrue(TEXT("Strictly increasing across rollover"), Current > Previous);
		}

		Previous = Current;
	}

	// The 4097th value (index 4096) must have advanced the timestamp beyond the fixed clock value.
	FGuid ParsedGuid;
	FGuid::ParseExact(Previous, EGuidFormats::DigitsWithHyphensLower, ParsedGuid);
	const uint64 EncodedTimestamp = (static_cast<uint64>(ParsedGuid.A) << 16) | (static_cast<uint64>(ParsedGuid.B) >> 16);
	TestTrue(TEXT("Timestamp advanced past fixed clock on rollover"), EncodedTimestamp > 5000000ULL);

	return true;
}

namespace
{
	class FPostHogUuidV7WorkerThread : public FRunnable
	{
	public:
		FPostHogUuidV7WorkerThread(PostHogUuidV7::FGenerator& InGenerator, int32 InIterations)
			: Generator(InGenerator)
			, Iterations(InIterations)
		{
		}

		virtual uint32 Run() override
		{
			for (int32 Index = 0; Index < Iterations; ++Index)
			{
				const FString Value = Generator.Generate(
					[]() -> uint64
					{
						const int64 TicksSinceEpoch = FDateTime::UtcNow().GetTicks() - FDateTime(1970, 1, 1).GetTicks();
						return static_cast<uint64>(TicksSinceEpoch / ETimespan::TicksPerMillisecond);
					},
					[]() -> FGuid
					{
						return FGuid::NewGuid();
					});

				{
					FScopeLock ScopeLock(&ResultsLock);
					Results.Add(Value);
				}
			}

			return 0;
		}

		TArray<FString> Results;
		FCriticalSection ResultsLock;

	private:
		PostHogUuidV7::FGenerator& Generator;
		int32 Iterations;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogUuidV7ConcurrencyTest, "UnrealHog.Utilities.UuidV7.Concurrency", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogUuidV7ConcurrencyTest::RunTest(const FString& Parameters)
{
	PostHogUuidV7::FGenerator Generator;

	constexpr int32 NumThreads = 4;
	constexpr int32 IterationsPerThread = 50;

	TArray<TUniquePtr<FPostHogUuidV7WorkerThread>> Workers;
	TArray<FRunnableThread*> Threads;

	for (int32 Index = 0; Index < NumThreads; ++Index)
	{
		Workers.Add(MakeUnique<FPostHogUuidV7WorkerThread>(Generator, IterationsPerThread));
		Threads.Add(FRunnableThread::Create(Workers.Last().Get(), *FString::Printf(TEXT("PostHogUuidV7TestWorker%d"), Index)));
	}

	TSet<FString> AllResults;

	for (int32 Index = 0; Index < NumThreads; ++Index)
	{
		Threads[Index]->WaitForCompletion();
		delete Threads[Index];

		for (const FString& Value : Workers[Index]->Results)
		{
			TestFalse(TEXT("Value non-empty"), Value.IsEmpty());
			TestFalse(TEXT("No duplicate across threads"), AllResults.Contains(Value));
			AllResults.Add(Value);
		}
	}

	TestEqual(TEXT("Expected total count"), AllResults.Num(), NumThreads * IterationsPerThread);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
