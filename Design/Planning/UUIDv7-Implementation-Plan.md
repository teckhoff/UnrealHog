# UUIDv7 implementation plan

## Status

- **State:** Ready for implementation
- **Scope:** Add an RFC 9562 UUIDv7 generator to the UnrealHog runtime module and replace the plugin's current event and session `FGuid` generation paths.
- **Affected parity behavior:** Event UUID generation and session identifier generation.
- **Primary standard:** [RFC 9562, UUID Version 7](https://www.rfc-editor.org/rfc/rfc9562.html#section-5.7)

## Problem

UnrealHog currently calls `FGuid::NewGuid()` for every event UUID and for the runtime subsystem's session identifier. `FGuid` is a 128-bit value with useful parsing and formatting support, but `FGuid::NewGuid()` delegates generation to `FPlatformMisc::CreateGuid()`. The generated UUID version therefore depends on the target platform. Unreal Engine 5.8's Unix implementation creates UUIDv7 values, while Windows and other platform implementations do not provide the same guarantee.

PostHog identifiers created by this plugin must be UUIDv7 on every supported Unreal platform. The plugin needs one generator with a platform-independent bit layout and one canonical string representation rather than relying on each platform's default GUID version.

## Current inventory

| Location | Current behavior | Required change |
| --- | --- | --- |
| `UnrealHog/Source/UnrealHog/Private/Events/PostHogEvent.cpp` | Constructs `EventUuid` with `FGuid::NewGuid()` and formats it as lowercase hyphenated text. | Generate the event UUID through the UUIDv7 utility. |
| `UnrealHog/Source/UnrealHog/Public/Subsystems/PostHogRuntimeSubsystem.h` | Stores `SessionId` as an `FGuid`. | Store the already canonical identifier as an `FString`. |
| `UnrealHog/Source/UnrealHog/Private/Subsystems/PostHogRuntimeSubsystem.cpp` | Creates the session with `FGuid::NewGuid()` and formats it at each event capture. | Generate one UUIDv7 string at subsystem initialization and pass it through unchanged. |

No other production `FGuid`, `FGuid::NewGuid()`, or `EGuidFormats` usage currently exists in the plugin.

## Reference behavior

The smallest relevant Unity reference set is:

- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/Utilities/UuidV7.cs`
- `Design/Reference/posthog-unity/tests/PostHog.Unity.Tests/UuidV7Tests.cs`
- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/Models/PostHogEvent.cs`
- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/Core/SessionManager.cs`

The Unity SDK emits lowercase, hyphenated UUIDv7 strings, uses a counter to keep values generated within one millisecond ordered, and uses the generator for both event and session identifiers. UnrealHog must preserve those observable properties without copying the C# implementation or its use of `System.Random`.

The relevant Unreal Engine 5.8 implementation points are:

- `CI/UnrealEngine/Engine/Source/Runtime/Core/Public/Misc/Guid.h`
- `CI/UnrealEngine/Engine/Source/Runtime/Core/Private/Misc/Guid.cpp`
- `CI/UnrealEngine/Engine/Source/Runtime/Core/Private/Unix/UnixPlatformGuid.cpp`
- `CI/UnrealEngine/Engine/Source/Runtime/Core/Private/Windows/WindowsPlatformMisc.cpp`

`Guid.h` establishes that `FGuid` consists of four `uint32` components. `Guid.cpp` establishes that `DigitsWithHyphensLower` renders those numeric components as `AAAAAAAA-BBBB-BBBB-CCCC-CCCCDDDDDDDD` and that `FGuid::NewGuid()` delegates generation to the platform. The Unix and Windows implementations demonstrate why the plugin cannot infer a UUID version from the `FGuid` API alone.

## Required UUID layout

The utility must create the RFC 9562 UUIDv7 layout in network-significant order:

| UUID bits | Width | Value |
| --- | ---: | --- |
| 0-47 | 48 | Unsigned Unix timestamp in milliseconds. |
| 48-51 | 4 | Version, fixed to binary `0111`. |
| 52-63 | 12 | Per-millisecond monotonic counter. |
| 64-65 | 2 | RFC variant, fixed to binary `10`. |
| 66-127 | 62 | Random data obtained through Unreal's platform GUID generation. |

The `FGuid` components must be assembled numerically as follows:

```text
A = timestamp bits 47..16
B = timestamp bits 15..0 | version 7 | 12-bit counter
C = variant 10 | high 30 random bits
D = low 32 random bits
```

Do not `memcpy` RFC-order bytes into an `FGuid`. The in-memory byte order of its `uint32` fields is platform-dependent. Constructing `A`, `B`, `C`, and `D` explicitly makes `FGuid::ToString(EGuidFormats::DigitsWithHyphensLower)` produce the RFC field order on every target.

## Proposed design

### Private utility

Add a private runtime utility:

- `UnrealHog/Source/UnrealHog/Private/Utilities/PostHogUuidV7.h`
- `UnrealHog/Source/UnrealHog/Private/Utilities/PostHogUuidV7.cpp`

The production-facing operation should return `FString`, for example `PostHogUuidV7::New()`. It must not be exported from the module, exposed to Blueprint, or placed in the plugin's public headers.

Returning `FString` is intentional. Event UUIDs and the current session identifier are wire and persistence values, not values on which UnrealHog performs GUID arithmetic. The utility owns the only permitted UUID formatting choice and prevents callers from accidentally selecting an incompatible `EGuidFormats` value.

Internally, split the implementation into:

1. A stateful generator that selects the effective timestamp and counter under a lock.
2. A pure packer that accepts a timestamp, the 12-bit `rand_a` value, and 62 `rand_b` bits, constructs an `FGuid(A, B, C, D)`, and returns lowercase hyphenated text. Production passes the counter as `rand_a`; tests can pass the RFC vector's `rand_a` value directly.
3. A process-local production instance used by `PostHogUuidV7::New()`.

Keep the generator and deterministic packing seam private to the runtime module so automation tests can inject timestamps and entropy without adding a public SDK contract.

### Time source

Compute Unix milliseconds using integer ticks, not floating-point conversion:

```text
(FDateTime::UtcNow().GetTicks() - FDateTime(1970, 1, 1).GetTicks())
    / ETimespan::TicksPerMillisecond
```

Validate that the result fits the UUIDv7 48-bit unsigned timestamp field before packing it. A production-time range failure should emit an `ensure` and return no identifier rather than silently wrapping into a different time.

### Entropy source

Call `FGuid::NewGuid()` only inside the UUIDv7 utility and use its lower 62 payload bits as the random input. This keeps platform entropy acquisition behind Unreal's existing abstraction while the plugin, rather than the platform GUID implementation, owns the timestamp, version, and variant fields.

Treat the entropy source as infallible. `FGuid::NewGuid()` cannot fail on any supported platform — platform implementations never signal failure by returning the zero GUID — so generation performs no validity check, retry, or error path on the entropy value. This matches the Unity SDK, where `Guid.NewGuid()` is likewise infallible, and keeps `Generate()` from returning an empty identifier that every caller would otherwise have to handle.

This approach deliberately does not use `FMath::Rand`, `FRandomStream`, MAC addresses, or device identifiers. Those alternatives either have weak collision resistance, expose host information, or introduce deterministic game RNG state into analytics identifiers.

### Monotonic state and concurrency

Protect `bHasGeneratedValue`, `LastTimestampMilliseconds`, and the 12-bit counter with `FCriticalSection` and `FScopeLock`. The explicit boolean is required so Unix epoch millisecond zero is distinguishable from uninitialized state.

For each generation:

1. On the first call, use the wall clock and initialize the counter to zero.
2. If wall-clock milliseconds are greater than the last effective timestamp, use the wall clock and reset the counter to zero.
3. If wall-clock milliseconds equal or precede the last effective timestamp, retain the last timestamp and increment the counter.
4. If the counter would exceed `0xFFF`, increment the effective timestamp by one millisecond and reset the counter to zero.
5. Reject the operation if logical advancement would exceed the 48-bit timestamp range.
6. Store the chosen timestamp and counter before releasing the lock.
7. Pack new random data into the remaining 62 bits.

Advancing the logical timestamp on counter rollover is permitted by RFC 9562 and avoids a busy wait on the game thread. Retaining the prior timestamp during a clock rollback also keeps generated strings strictly increasing within the process. Generator state remains process-local and is not persisted; the random field provides cross-process collision resistance.

The lock defines generation order across threads. The utility must not expose unlocked test or production paths that mutate the shared state.

### Canonical representation

Every successful result must be exactly 36 ASCII characters in the form:

```text
xxxxxxxx-xxxx-7xxx-[89ab]xxx-xxxxxxxxxxxx
```

Use lowercase hexadecimal and no braces or `urn:uuid:` prefix. The resulting value remains safe for the existing queue filename scheme.

## Integration plan

### 1. Implement and test the generator

Add the private utility and its automation tests before changing callers. The implementation must depend only on the existing `Core` module.

Add `UnrealHog/Source/UnrealHog/Private/Tests/PostHogUuidV7Tests.cpp`, guarded with `WITH_DEV_AUTOMATION_TESTS`. Only adjust `UnrealHog/Source/UnrealHog/UnrealHog.Build.cs` if compilation demonstrates that an additional existing Unreal test dependency is required; do not add a third-party UUID library.

### 2. Replace event UUID generation

Change `FPostHogEvent` construction to call the UUIDv7 utility. Keep `EventUuid` as an `FString`, continue writing the same value to the JSON `uuid` field, and continue using that same value as the queue storage key. Update the field comment to state UUIDv7 explicitly.

If generation returns an empty string, `CaptureEvent` must log the failure and return immediately after construction, before applying properties or calling `FPostHogEventQueue::Enqueue`. Keep the queue's existing empty-ID rejection as defense in depth, not as the primary failure path.

### 3. Replace subsystem session GUID state

Change `UPostHogRuntimeSubsystem::SessionId` from `FGuid` to `FString`. Generate it once near the start of `Initialize()`, after collection permission has allowed subsystem creation but before constructing storage, queue, or HTTP collaborators. Pass the string directly to `FPostHogEvent`.

Preserve the current lifecycle and meaning of this field. This task changes only the identifier version and representation; it does not redesign identity or session semantics.

If session UUID generation fails, log and return from `Initialize()` before constructing those collaborators. Add a readiness guard to `CaptureEvent` so a partially initialized subsystem returns without dereferencing a null queue. This leaves capture unavailable and prevents an event payload, queue record, file, or HTTP request.

### 4. Enforce the migration boundary

After integration, search the runtime source for direct GUID generation. `FGuid::NewGuid()` and `EGuidFormats` may appear only inside the UUIDv7 utility and its tests. There must be no `FGuid SessionId` declaration or direct event UUID generation remaining.

Future anonymous IDs, session replay IDs, and other PostHog UUID fields should use this utility when those features are added. That future integration is not part of this task because those production paths do not yet exist in UnrealHog.

## Persistence and compatibility

No persisted-data migration is required.

- Existing queued events may have UUIDv4 filenames and JSON `uuid` values. Continue loading, sending, and deleting them unchanged.
- New event UUIDv7 strings have the same 36-character lowercase hyphenated shape and remain valid filenames under the current storage provider.
- Never regenerate an event UUID when an event is loaded or retried. The stored filename/key and the payload `uuid` must remain identical for the event's lifetime.
- The current subsystem session identifier is not persisted, so changing its in-memory type requires no state conversion.

Do not reject a stored event merely because its UUID is not version 7. UUIDv7 validation applies at new identifier generation boundaries, not at legacy queue read boundaries.

## Consent boundary

UUID generation must preserve the repository's opt-in invariant.

- Do not generate an identifier during module load or static initialization. A static generator object may hold zeroed state, but it must not acquire time or entropy until `New()` is called.
- Generate the subsystem identifier only after analytics collection is permitted and the subsystem is allowed to initialize.
- Generate an event UUID only as part of an allowed capture operation.
- A failed generation must not result in a JSON payload, queue file, or HTTP request.

## Test plan

### Deterministic automation tests

1. **RFC packing vector:** Inject timestamp `1645557742000`, `rand_a` value `0xCC3`, and the RFC `rand_b` bits; assert `017f22e2-79b0-7cc3-98c4-dc0c0c07398f`.
2. **Canonical text:** Assert 36 characters, hyphens at positions 8, 13, 18, and 23, lowercase hexadecimal elsewhere, and successful `FGuid::ParseExact` using `DigitsWithHyphensLower`.
3. **Version and variant:** Assert character 14 is `7` and character 19 is one of `8`, `9`, `a`, or `b`; also inspect the packed component masks.
4. **Timestamp:** Inject boundary millisecond values and assert the first 12 hexadecimal digits reproduce the low 48 timestamp bits without host-endian dependence.
5. **Same-millisecond ordering:** Generate multiple values at one injected timestamp and assert uniqueness and strict lexical increase.
6. **Clock rollback:** Supply a timestamp earlier than the previous call and assert the effective timestamp does not decrease and the next value sorts later.
7. **Counter rollover:** Generate through counter `0xFFF` with a fixed clock and assert the next value advances the logical timestamp without blocking or duplicating.
8. **Concurrency:** Generate values from multiple worker threads, then assert the expected count, no empty values, no duplicates, and valid version/variant bits.

### Integration automation tests

1. Construct an event and assert its JSON `uuid` is UUIDv7 and matches `GetEventId()`.
2. Enqueue an event through isolated storage and HTTP fakes and assert its storage key, persisted JSON UUID, and batch payload UUID are identical.
3. Initialize the permitted subsystem path and assert the generated identifier passed as the current distinct ID is UUIDv7.
4. Exercise the denied-consent path and assert no generator call, event payload, queue record, file, or HTTP request occurs.
5. Seed storage with a lowercase UUIDv4 event key and assert it remains loadable and deletable, proving backward compatibility.

Tests must not contact PostHog or require credentials.

### Platform verification

Zeroshot workers and validators run in WSL. Perform source inspection, repository-provided platform-neutral checks, test fixture review, and searches for forbidden call sites, then run the Windows-side Unreal Automation gate:

1. If the required `CI` symlinks are missing from the worktree, run `Scripts/ci-paths.sh` first.
2. Run `Scripts/run-windows-tests.sh` to build the Editor target and execute the UnrealHog Automation suite.
3. Verify the focused UUIDv7 tests and the affected event, queue, consent, and subsystem tests pass.
4. Verify at least one generated event payload in the isolated test harness has a lowercase UUIDv7 `uuid` and no direct `FGuid` generation path remains in the callers.
5. Record the script output as a required Zeroshot validation gate.

## Acceptance criteria

- Every newly created UnrealHog event UUID is an RFC 9562 UUIDv7 string with a millisecond Unix timestamp, version `7`, RFC variant `10`, and lowercase hyphenated formatting.
- The subsystem's newly created session identifier is UUIDv7 and remains stable for its existing lifetime.
- Generated identifiers are unique and strictly increasing in process generation order for equal timestamps, clock rollback, and 12-bit counter rollover.
- Generator state is thread-safe and counter rollover never busy-waits or sleeps on the game thread.
- Callers store UUIDs as canonical `FString` values; direct `FGuid::NewGuid()` and `EGuidFormats` usage is confined to the UUIDv7 utility and tests.
- The RFC UUIDv7 test vector passes, proving correct field placement and byte order.
- Event storage keys, persisted JSON UUIDs, retry UUIDs, and outgoing payload UUIDs remain identical.
- Existing persisted UUIDv4 events continue to load, send, and delete without rewriting.
- No identifier or downstream analytics artifact is created before collection is permitted.
- Focused automation tests pass in a Windows Unreal Engine 5.8 build without live PostHog credentials.

## Exclusions

- Modifying Unreal Engine's `FGuid`, `FPlatformMisc`, or any file under `Design/Reference`.
- Replacing non-PostHog GUID usage elsewhere in a host game or in Unreal Engine.
- Redesigning distinct identity, anonymous identity persistence, session rotation, or the `$session_id` property.
- Adding session replay or generating identifiers for features not yet implemented in UnrealHog.
- Rewriting or deleting already queued UUIDv4 events.
- Exposing UUID generation as a Blueprint or public plugin API.
- Adding a third-party UUID or cryptography dependency.

## Implementation risks and mitigations

| Risk | Mitigation |
| --- | --- |
| Host-endian `FGuid` memory layout corrupts timestamp or version placement. | Construct `A`, `B`, `C`, and `D` numerically and lock behavior with the RFC vector. |
| Platform `FGuid::NewGuid()` versions differ. | Use it only for entropy; overwrite all UUIDv7 structural fields in plugin code. |
| A clock rollback breaks lexical ordering. | Retain the last effective timestamp and advance the locked counter. |
| More than 4096 IDs are generated in one millisecond. | Advance a logical millisecond and reset the counter instead of blocking. |
| Concurrent capture returns duplicates or races generator state. | Serialize timestamp/counter selection with `FCriticalSection`; run a multi-thread uniqueness test. |
| A generation failure leaks an invalid event into storage. | Return failure explicitly and stop before event construction/enqueue. |
| Tightening validation strands legacy queued events. | Validate only new generation and accept existing stored IDs as opaque strings. |
| A future feature bypasses UUIDv7. | Keep one private utility and add a source-search check for direct `FGuid::NewGuid()` call sites. |

## Suggested change sequence

Keep the implementation in one scoped pull request:

1. Add the private generator, deterministic seams, and focused automation tests.
2. Replace event UUID creation and cover persistence/payload identity.
3. Replace subsystem session state and cover consent/failure behavior.
4. Run source-search and platform-neutral checks in WSL.
5. Run `Scripts/run-windows-tests.sh` and record the passing Unreal Automation output as the Zeroshot validation gate; run `Scripts/ci-paths.sh` first if the required `CI` symlinks are missing from the worktree.

The pull request must identify the affected event UUID and session identifier parity behaviors, list the Windows execution environment, and include the repository's required automated-agent notice.
