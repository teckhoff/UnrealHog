#pragma once

#include "CoreMinimal.h"
#include "Events/PostHogEventProperties.h"

class IPostHogStorageProvider;
struct FPostHogEvent;

/**
 * @brief Private, storage-persisted set of reusable properties merged into every future event.
 *
 * Not a UObject: owned by FPostHogConsentController and exercised directly in automation tests
 * via an injectable IPostHogStorageProvider, matching the FPostHogIdentityManager pattern.
 * Construction performs no storage I/O; LoadOrCreate() must be called only after analytics
 * collection is permitted. Unlike FPostHogIdentityManager, a missing/malformed/unsupported-version
 * load never rewrites storage: super properties are user-authored data, not a regenerable
 * identifier, so a corrupt record is left untouched in case it is recoverable.
 */
class FPostHogSuperPropertiesManager
{
public:
	static const TCHAR* const StateKey;
	static constexpr int32 CurrentSchemaVersion = 1;

	// Loads persisted super properties from Storage. On missing state, leaves the property set
	// empty without writing anything. On parse failure, non-object shape, or unsupported schema
	// version, logs a warning and leaves the property set empty WITHOUT rewriting storage.
	void LoadOrCreate(IPostHogStorageProvider& Storage);

	// Registers Key=Value, overwriting any existing registration for Key, and persists the full
	// set. A blank/whitespace-only Key or a reserved SDK-owned key is rejected with a logged
	// warning and no storage write.
	void Register(const FString& Key, const FPostHogEventProperty& Value, IPostHogStorageProvider& Storage);

	// Removes Key if present and persists the removal. No-op (no storage write) if Key was not
	// registered.
	void Unregister(const FString& Key, IPostHogStorageProvider& Storage);

	// Removes all registered properties and persists the empty set.
	void Clear(IPostHogStorageProvider& Storage);

	// Applies all registered super properties onto Event. Callers must invoke this before
	// layering call-supplied and SDK-owned properties so those take precedence.
	void ApplyTo(FPostHogEvent& Event) const;

	int32 Num() const { return Properties.Num(); }

private:
	void PersistState(IPostHogStorageProvider& Storage) const;

	TMap<FString, FPostHogEventProperty> Properties;
};
