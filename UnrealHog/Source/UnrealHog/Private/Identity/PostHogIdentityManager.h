#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

class IPostHogStorageProvider;

/**
 * @brief Private, storage-persisted anonymous/identified identity, independent of sessions.
 *
 * Not a UObject: owned by FPostHogConsentController and exercised directly in automation
 * tests via an injectable UUID generator and IPostHogStorageProvider. Construction performs
 * no storage I/O and generates no identifier; LoadOrCreate() must be called only after
 * analytics collection is permitted, matching Unity's IdentityManager but deferring the
 * eager constructor-time load behind the opt-in gate.
 */
class FPostHogIdentityManager
{
public:
	using FUuidGenerator = TFunction<FString()>;

	static const TCHAR* const StateKey;
	static constexpr int32 CurrentSchemaVersion = 1;

	explicit FPostHogIdentityManager(FUuidGenerator InUuidGenerator = nullptr);

	// Loads persisted identity state from Storage, or generates and persists a fresh anonymous
	// identity when state is missing, malformed, or an unsupported schema version.
	void LoadOrCreate(IPostHogStorageProvider& Storage);

	const FString& GetAnonymousId() const { return AnonymousId; }
	const FString& GetEffectiveDistinctId() const { return DistinctId.IsEmpty() ? AnonymousId : DistinctId; }
	bool IsIdentified() const { return bIsIdentified; }

	// Persists NewDistinctId. Returns the prior anonymous id only on first identification
	// (bIsIdentified was false); returns empty on repeated identify (no relink). Caller
	// (FPostHogConsentController) is responsible for rejecting blank NewDistinctId.
	FString Identify(const FString& NewDistinctId, IPostHogStorageProvider& Storage);

	// Clears distinct id, identified flag, and groups; regenerates AnonymousId via
	// UuidGenerator unless bReuseAnonymousId is true. Always persists.
	void Reset(IPostHogStorageProvider& Storage, bool bReuseAnonymousId);

	// Returns a copy of current group membership; TMap copy-by-value is a deep copy, so
	// callers cannot mutate manager state through the result.
	TMap<FString, FString> GetGroups() const { return Groups; }

	// Trims GroupType/GroupKey and replaces the membership for that group type, leaving
	// other group types untouched. Returns false and mutates no state (no persist) if
	// either trimmed value is empty.
	bool SetGroup(const FString& GroupType, const FString& GroupKey, IPostHogStorageProvider& Storage);

	// Clears all group membership and persists.
	void ClearGroups(IPostHogStorageProvider& Storage);

private:
	void PersistState(IPostHogStorageProvider& Storage);

	FUuidGenerator UuidGenerator;

	FString AnonymousId;
	FString DistinctId;
	bool bIsIdentified = false;
	TMap<FString, FString> Groups;
};
