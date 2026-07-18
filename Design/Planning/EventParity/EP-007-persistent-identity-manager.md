# EP-007: Add persistent anonymous and identified identity state

## Status and dependencies

- **State:** Completed
- **Blocked by:** EP-002
- **Blocks:** EP-008, EP-010, EP-011, EP-014, EP-017, EP-029
- **Parity row:** Unity `IdentityManager`

## Goal

Stop using the game-instance session UUID as `distinct_id` and maintain a persistent anonymous identity independently of sessions.

## Required changes

- Add a private identity manager backed by `IPostHogStorageProvider` state key `identity` with an explicit schema version.
- Persist anonymous ID, optional identified ID, identified state, and group membership data.
- Generate the anonymous UUIDv7 lazily only after EP-002 grants collection permission.
- Expose private queries for effective distinct ID, anonymous ID, and identified state.
- Recover safely from missing, malformed, or newer-version state without crashing capture.
- Attach the currently generated session id to all events for that session under the `$session_id` key.

## Acceptance criteria

- First permitted initialization creates and persists one anonymous UUIDv7.
- Restart with the same storage reuses it; identified state selects the known ID as `distinct_id`.
- Malformed state is replaced with a valid anonymous identity and a warning.
- No identity state read/write or UUID generation occurs before consent.
- Tests use in-memory storage and never touch the project's real Saved directory.
- Run `Scripts/ci-paths.sh` first if the required `CI` symlinks are missing from the worktree, then run `Scripts/run-windows-tests.sh`; the Unreal Automation tests must pass and their output must be recorded as a Zeroshot validation gate.

## Exclusions

- Do not add public identify/reset/alias APIs; EP-008 owns them.
- Do not create or rotate sessions.

## Unity references

- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/Core/IdentityManager.cs`
- `Design/Reference/posthog-unity/tests/PostHog.Unity.Tests/PostHogSDKTests.cs`
