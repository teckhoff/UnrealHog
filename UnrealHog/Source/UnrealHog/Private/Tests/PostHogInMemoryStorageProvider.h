#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Storage/PostHogStorageProvider.h"

/**
 * @brief In-memory-only IPostHogStorageProvider for automation tests, never touching disk.
 *
 * Mirrors Unity's InMemoryStorageProvider test double so identity/consent tests can assert on
 * persisted state without creating or cleaning up real files or directories.
 */
class FPostHogInMemoryStorageProvider final : public IPostHogStorageProvider
{
public:
	virtual bool SaveEvent(const FString& EventId, const FString& EventJson) override
	{
		Events.Add(EventId, EventJson);
		return true;
	}
	using IPostHogStorageProvider::SaveEvent;

	virtual bool LoadEvent(const FString& EventId, FString& EventJson) override
	{
		const FString* Found = Events.Find(EventId);
		if (!Found)
		{
			return false;
		}
		EventJson = *Found;
		return true;
	}

	virtual bool DeleteEvent(const FString& EventId) override
	{
		return Events.Remove(EventId) > 0;
	}

	virtual bool ClearEvents() override
	{
		Events.Empty();
		return true;
	}

	virtual TArray<FString> GetEventIds() override
	{
		TArray<FString> Ids;
		Events.GetKeys(Ids);
		Ids.Sort();
		return Ids;
	}

	virtual int32 GetEventCount() override
	{
		return Events.Num();
	}

	virtual bool SaveState(const FString& StateKey, const FString& StateJson) override
	{
		State.Add(StateKey, StateJson);
		return true;
	}
	using IPostHogStorageProvider::SaveState;

	virtual bool LoadState(const FString& StateKey, FString& StateJson) override
	{
		const FString* Found = State.Find(StateKey);
		if (!Found)
		{
			return false;
		}
		StateJson = *Found;
		return true;
	}

	virtual bool DeleteState(const FString& StateKey) override
	{
		return State.Remove(StateKey) > 0;
	}

private:
	TMap<FString, FString> Events;
	TMap<FString, FString> State;
};

#endif // WITH_DEV_AUTOMATION_TESTS
