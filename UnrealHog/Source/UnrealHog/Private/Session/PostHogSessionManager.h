#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Misc/DateTime.h"
#include "Templates/Function.h"

/**
 * @brief Private, in-memory rotating session id manager, independent of user identity.
 *
 * Not a UObject: owned by FPostHogConsentController and exercised directly in automation
 * tests via injectable clock and id generator seams. Never persisted; a fresh instance
 * always starts with no session, matching Unity's SessionManager.
 *
 * Diverges from Unity's SessionManager by additionally gating session creation on
 * bIsCollectionPermitted: no session id may be generated before analytics collection is
 * permitted, including via the explicit StartNewSession() entry point.
 */
class FPostHogSessionManager
{
public:
	using FNowProvider = TFunction<FDateTime()>;
	using FSessionIdGenerator = TFunction<FString()>;

	explicit FPostHogSessionManager(FNowProvider InNowProvider = nullptr, FSessionIdGenerator InSessionIdGenerator = nullptr);

	// Returns the current session id, rotating or starting one as needed. Empty when
	// backgrounded with no active session, or when collection is not permitted.
	FString GetSessionId();

	// Records activity, potentially rotating or clearing an expired session. Callers should
	// invoke this only after an event has been accepted for enqueue.
	void Touch();

	// Starts a new session immediately, subject to the collection-permitted gate.
	void StartNewSession();

	void OnForeground();
	void OnBackground();

	// Clears any active session.
	void EndSession();

	void SetCollectionPermitted(bool bPermitted);

private:
	FString GetSessionIdInternal(const FDateTime& Now);
	void TouchInternal(const FDateTime& Now);
	bool HandleExpiredSession(const FDateTime& Now);
	bool HasExpiredSession(const FDateTime& Now) const;
	FString StartNewSessionInternal(const FDateTime& Now);
	void ClearSessionInternal();

	FNowProvider NowProvider;
	FSessionIdGenerator SessionIdGenerator;

	FCriticalSection Lock;
	FString SessionId;
	FDateTime SessionStartTime;
	FDateTime LastActivityTime;
	bool bIsInForeground = true;
	bool bIsCollectionPermitted = false;
};
