# EP-009: Add an independent rotating session manager

## Status and dependencies

- **State:** Blocked
- **Blocked by:** EP-002
- **Blocks:** EP-016, EP-027, EP-029
- **Parity row:** Unity `SessionManager` and `$session_id`

## Goal

Maintain an in-memory session ID distinct from user identity and attach it to captured events.

## Required changes

- Add a private session manager with an injectable UTC clock.
- Start UUIDv7 sessions only while foregrounded and collection is permitted.
- Rotate after more than 30 minutes inactivity or more than 24 hours total duration.
- Clear an expired session while backgrounded and start a new one on foreground.
- Add `$session_id` during central capture composition and touch activity only after an event is accepted for enqueue.

## Acceptance criteria

- Multiple events in an active window share one session ID while retaining a separate distinct ID.
- Boundary tests cover inactivity, maximum duration, foreground/background, explicit new session, and clock movement.
- Reset integration can start a new session without generating it before consent.
- Session state is not persisted across game-instance lifetimes.
- Windows Automation uses a fake clock; no sleep-based test is allowed.

## Exclusions

- Do not implement session replay.
- Do not use the session ID as `distinct_id`.

## Unity references

- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/Core/SessionManager.cs`
- `Docs/Reference/posthog-unity/tests/PostHog.Unity.Tests/SessionManagerTests.cs`
