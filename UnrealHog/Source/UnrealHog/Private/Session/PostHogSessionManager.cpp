#include "Session/PostHogSessionManager.h"

#include "Misc/Timespan.h"
#include "Utilities/PostHogUuidV7.h"

namespace
{
	const FTimespan SessionTimeout = FTimespan::FromMinutes(30.0);
	const FTimespan MaxSessionLength = FTimespan::FromHours(24.0);
}

FPostHogSessionManager::FPostHogSessionManager(FNowProvider InNowProvider, FSessionIdGenerator InSessionIdGenerator) :
	NowProvider(InNowProvider ? MoveTemp(InNowProvider) : FNowProvider(&FDateTime::UtcNow)),
	SessionIdGenerator(InSessionIdGenerator ? MoveTemp(InSessionIdGenerator) : FSessionIdGenerator(&PostHogUuidV7::New))
{
}

FString FPostHogSessionManager::GetSessionId()
{
	FScopeLock ScopeLock(&Lock);
	return GetSessionIdInternal(NowProvider());
}

void FPostHogSessionManager::Touch()
{
	FScopeLock ScopeLock(&Lock);
	TouchInternal(NowProvider());
}

void FPostHogSessionManager::StartNewSession()
{
	FScopeLock ScopeLock(&Lock);
	StartNewSessionInternal(NowProvider());
}

void FPostHogSessionManager::OnForeground()
{
	FScopeLock ScopeLock(&Lock);
	bIsInForeground = true;
	const FDateTime Now = NowProvider();

	if (SessionId.IsEmpty())
	{
		StartNewSessionInternal(Now);
		return;
	}

	if (HandleExpiredSession(Now))
	{
		return;
	}

	LastActivityTime = Now;
}

void FPostHogSessionManager::OnBackground()
{
	FScopeLock ScopeLock(&Lock);
	bIsInForeground = false;

	if (!SessionId.IsEmpty())
	{
		LastActivityTime = NowProvider();
	}
}

void FPostHogSessionManager::EndSession()
{
	FScopeLock ScopeLock(&Lock);
	ClearSessionInternal();
}

void FPostHogSessionManager::SetCollectionPermitted(bool bPermitted)
{
	FScopeLock ScopeLock(&Lock);
	bIsCollectionPermitted = bPermitted;
}

FString FPostHogSessionManager::GetSessionIdInternal(const FDateTime& Now)
{
	if (SessionId.IsEmpty())
	{
		if (bIsInForeground)
		{
			return StartNewSessionInternal(Now);
		}

		return FString();
	}

	HandleExpiredSession(Now);
	return SessionId;
}

void FPostHogSessionManager::TouchInternal(const FDateTime& Now)
{
	if (SessionId.IsEmpty())
	{
		return;
	}

	if (HandleExpiredSession(Now))
	{
		return;
	}

	LastActivityTime = Now;
}

bool FPostHogSessionManager::HandleExpiredSession(const FDateTime& Now)
{
	if (!HasExpiredSession(Now))
	{
		return false;
	}

	if (bIsInForeground)
	{
		StartNewSessionInternal(Now);
	}
	else
	{
		ClearSessionInternal();
	}

	return true;
}

bool FPostHogSessionManager::HasExpiredSession(const FDateTime& Now) const
{
	if (SessionId.IsEmpty())
	{
		return false;
	}

	if (Now - SessionStartTime > MaxSessionLength)
	{
		return true;
	}

	if (Now - LastActivityTime > SessionTimeout)
	{
		return true;
	}

	return false;
}

FString FPostHogSessionManager::StartNewSessionInternal(const FDateTime& Now)
{
	if (!bIsCollectionPermitted)
	{
		ClearSessionInternal();
		return FString();
	}

	SessionId = SessionIdGenerator();
	SessionStartTime = Now;
	LastActivityTime = Now;

	return SessionId;
}

void FPostHogSessionManager::ClearSessionInternal()
{
	if (SessionId.IsEmpty())
	{
		return;
	}

	SessionId.Empty();
	SessionStartTime = FDateTime();
	LastActivityTime = FDateTime();
}
