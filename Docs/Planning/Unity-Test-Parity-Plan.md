# Unity test parity plan for the current SDK surface

## Status

- **State:** Ready for implementation
- **Unity reference baseline:** `Docs/Reference/posthog-unity` at commit `73e4b9d7ccc75c398b557b3c2779ff03985bf91e`
- **Scope:** Add Unreal automation coverage for behavior that already has a production counterpart in UnrealHog, and record the Unity tests that must wait for a corresponding Unreal feature or threading contract.
- **Primary reference directory:** `Docs/Reference/posthog-unity/tests/PostHog.Unity.Tests`

## Goal

Reach behavioral test parity with the part of the Unity SDK represented by the current UnrealHog implementation. Parity means preserving observable SDK behavior through idiomatic Unreal APIs; it does not mean reproducing C# constructors, exceptions, coroutines, reflection helpers, or Unity-specific filesystem internals.

The minimum reference set requested for this plan is fully in scope:

- `Docs/Reference/posthog-unity/tests/PostHog.Unity.Tests/PostHogEventTests.cs`
- `Docs/Reference/posthog-unity/tests/PostHog.Unity.Tests/PostHogConfigTests.cs`
- `Docs/Reference/posthog-unity/tests/PostHog.Unity.Tests/SdkInfoTests.cs`
- `Docs/Reference/posthog-unity/tests/PostHog.Unity.Tests/FileStorageProviderTests.cs`

An audit of every other Unity test file found additional coverage that is relevant now:

- all semantic assertions in `UuidV7Tests.cs`;
- the event and batch portions of `JsonSerializerTests.cs`;
- the settings-default and initialization-gating portions of `PostHogSettingsTests.cs`; and
- the missing-configuration and safe-no-op portions of `PostHogSDKTests.cs` that overlap the runtime subsystem's current API.

Feature flags, session replay, exception tracking, identity/session rotation, and their supporting generic containers are not implemented at the current level. Their tests are recorded as deferred rather than being pulled into this change.

## Current implementation boundary

The production behaviors available to test today are:

| UnrealHog area | Current implementation | Unity test source |
| --- | --- | --- |
| Event model | `FPostHogEvent`, scalar/object properties, timestamp, UUIDv7, and JSON projection | `PostHogEventTests.cs`; event subset of `JsonSerializerTests.cs` |
| Batch payload | `FPostHogBatchPayload` and JSON projection | batch subset of `JsonSerializerTests.cs` |
| Configuration | `UPostHogDeveloperSettings`, US/EU/Custom host resolution, delivery limits, profile/log enums | `PostHogConfigTests.cs`; relevant subset of `PostHogSettingsTests.cs` |
| SDK metadata | `PostHogSdkInfo` reads library and version information from the plugin descriptor and builds the User-Agent | `SdkInfoTests.cs` |
| Persistence | `IPostHogStorageProvider` and the synchronous `FPostHogFileStorageProvider` | synchronous behavior in `FileStorageProviderTests.cs` |
| UUID generation | private RFC 9562 UUIDv7 generator with deterministic test seams | `UuidV7Tests.cs`; UUID assertions in `PostHogEventTests.cs` |
| Runtime orchestration | opt-in gate, initialization, capture, periodic/manual flush, and safe unready guards | relevant subset of `PostHogSDKTests.cs` and `PostHogSettingsTests.cs` |

The corresponding Unreal implementation files are:

- `UnrealHog/Source/UnrealHog/Private/Events/PostHogEvent.h`
- `UnrealHog/Source/UnrealHog/Private/Events/PostHogEvent.cpp`
- `UnrealHog/Source/UnrealHog/Private/Events/PostHogBatchPayload.h`
- `UnrealHog/Source/UnrealHog/Private/Events/PostHogBatchPayload.cpp`
- `UnrealHog/Source/UnrealHog/Public/PostHogDeveloperSettings.h`
- `UnrealHog/Source/UnrealHog/Private/PostHogDeveloperSettings.cpp`
- `UnrealHog/Source/UnrealHog/Private/SDK/PostHogSdkInfo.h`
- `UnrealHog/Source/UnrealHog/Private/SDK/PostHogSdkInfo.cpp`
- `UnrealHog/Source/UnrealHog/Public/Storage/PostHogStorageProvider.h`
- `UnrealHog/Source/UnrealHog/Private/Storage/PostHogFileStorageProvider.h`
- `UnrealHog/Source/UnrealHog/Private/Storage/PostHogFileStorageProvider.cpp`
- `UnrealHog/Source/UnrealHog/Private/Utilities/PostHogUuidV7.h`
- `UnrealHog/Source/UnrealHog/Private/Utilities/PostHogUuidV7.cpp`
- `UnrealHog/Source/UnrealHog/Private/Subsystems/PostHogRuntimeSubsystem.cpp`

The baseline has known gaps that the parity tests should expose rather than encode as expected behavior:

- `bAnalyticsEnabled` currently defaults to `true`, conflicting with the repository invariant that collection is opt-in by default.
- Missing or whitespace API keys do not currently prevent subsystem initialization.
- Numeric `UDeveloperSettings` metadata constrains editor input, but values loaded from config do not have a single validation boundary equivalent to `PostHogConfig.Validate()`.
- Event and SDK metadata have no focused automation coverage.
- File storage has no automation coverage and is synchronous, whereas the Unity provider performs event writes asynchronously.
- Event and batch JSON projection have no focused automation coverage.

## Parity rules

Use these rules when translating a Unity assertion:

1. Test the behavior through the smallest Unreal production surface that owns it.
2. Parse JSON and compare fields and value types. Do not compare serialized object key order or whitespace.
3. Do not add a default constructor, a C#-style exception API, a public byte-oriented UUID API, or a second settings object only to make a Unity test look identical.
4. Treat the repository's opt-in requirement as an intentional override of Unity's auto-initialize default. Denied collection must create no identifier, event JSON, queue record, file, or HTTP request.
5. Run filesystem tests against a unique test-owned temporary directory and delete it after every test, including failure paths.
6. Keep all HTTP isolated. No parity test may require a PostHog key or contact a live PostHog host.
7. A deferred test is not silently dropped. It must have an activation condition tied to a production feature or an explicit contract decision.

## Detailed parity matrix: `PostHogEventTests.cs`

The Unity file contains 15 tests spread across three constructor shapes and UUID behavior. UnrealHog has one production constructor and enriches properties during construction, so constructor duplication should be consolidated while every behavior is retained.

| Unity test or group | Unreal disposition | Unreal assertion |
| --- | --- | --- |
| `TheDefaultConstructor.GeneratesUuid` | Adapt now | Construct `FPostHogEvent` through its supported constructor; assert `GetEventId()` is non-empty, canonical, and parseable. Do not add a default constructor. |
| `TheDefaultConstructor.SetsTimestamp` | Adapt now | Read `timestamp` from `ToJsonObject()` and assert it parses as ISO 8601 and falls within a small UTC window around construction. |
| `TheDefaultConstructor.InitializesEmptyProperties` | Adapt now | Assert the `properties` object exists. UnrealHog intentionally enriches it, so assert required SDK properties and absence of arbitrary user fields rather than literal emptiness. |
| `TheEventNameDistinctIdConstructor.SetsEventName` | Port now | Assert the JSON `event` field exactly preserves the supplied name. |
| `TheEventNameDistinctIdConstructor.SetsDistinctId` | Port now | Assert the JSON `distinct_id` field exactly preserves the supplied identifier. |
| `TheEventNameDistinctIdConstructor.GeneratesUuid` | Consolidate | Covered by the supported-constructor UUID test above. |
| `TheEventNameDistinctIdConstructor.SetsTimestamp` | Consolidate | Covered by the supported-constructor timestamp test above. |
| `TheEventNameDistinctIdConstructor.InitializesEmptyProperties` | Consolidate/adapt | Covered by the enriched-properties test above. |
| `TheFullConstructor.SetsEventName` and `SetsDistinctId` | Consolidate | Covered by the supported constructor. Do not introduce a C#-shaped full constructor. |
| `TheFullConstructor.CopiesProperties` | Adapt now | Apply string, number, and boolean values through `UPostHogEventProperties` or the event setters and assert their JSON types and values. |
| `TheFullConstructor.WithNullProperties_InitializesEmptyProperties` | Adapt now | Exercise `CaptureEvent(..., nullptr)` through an isolated subsystem/capture seam and assert it is safe, retains SDK-generated properties, and adds no user-supplied fields. |
| `TheFullConstructor.PropertiesAreCopiedNotReferenced` | Adapt now | Apply a properties object, mutate or destroy the source, and assert the event's projected JSON is unchanged. |
| `TheUuidProperty.IsVersion7Uuid` | Port now | Assert version nibble `7` and RFC variant `10`. |
| `TheUuidProperty.IsUniquePerEvent` | Port now | Construct multiple events and assert distinct IDs. |

No event behavior is deferred. Only Unity-specific constructor shapes and the literal expectation of an empty property dictionary are non-applicable; their observable behaviors are covered through the Unreal API.

Add `UnrealHog/Source/UnrealHog/Private/Tests/PostHogEventTests.cpp`. Where deterministic construction is useful, prefer a private clock/UUID factory seam over sleeps or broad public test APIs.

## Detailed parity matrix: `PostHogConfigTests.cs`

All 16 tests describe configuration behavior that has a current Unreal counterpart. The mechanics need adaptation because Unreal project configuration is a `UDeveloperSettings` object and should report invalid configuration without throwing C# exceptions.

| Unity test or group | Unreal disposition | Unreal assertion |
| --- | --- | --- |
| `TheDefaultValues.AreCorrect` | Port now with one intentional divergence | Assert the US host resolves to `https://us.i.posthog.com`, flush count 20, interval 30, queue size 1000, batch size 50, lifecycle capture enabled, `IdentifiedOnly`, warning logging, no anonymous-ID reuse, flush-on-quit enabled, and 3-second quit timeout. Also assert analytics collection defaults to **disabled**, as required by this repository. |
| `WithValidConfig_DoesNotThrow` | Adapt now | A valid settings snapshot returns a successful validation result and can initialize once collection is explicitly enabled. |
| `WithNullApiKey_ThrowsArgumentException` | Adapt now | Unreal has no null `FString`; empty and whitespace keys must make the configuration unusable and prevent collaborator construction. |
| `WithEmptyApiKey_ThrowsArgumentException` | Adapt now | Same invalid-key result; no storage, UUID, queue, or HTTP activity. |
| `WithWhitespaceApiKey_ThrowsArgumentException` | Adapt now | Trim for validation and reject whitespace-only content. |
| `WithNullEmptyOrWhitespaceHost_DefaultsHost` | Adapt now | US and EU always resolve to `https://us.i.posthog.com` and `https://eu.i.posthog.com`. A blank or whitespace-only Custom host normalizes to the US host at the validation boundary, matching Unity's fallback behavior. |
| `WithZeroFlushAt_ThrowsArgumentOutOfRangeException` | Adapt now | Reject a flush event count below 1 in the validation snapshot rather than relying only on editor metadata or silently clamping later. |
| `WithNegativeFlushAt_ThrowsArgumentOutOfRangeException` | Adapt now | Same as above for negative values. |
| `WithZeroFlushIntervalSeconds_ThrowsArgumentOutOfRangeException` | Adapt now | Reject values below 1 before scheduling a timer. |
| `WithZeroMaxQueueSize_ThrowsArgumentOutOfRangeException` | Adapt now | Reject values below 1 before queue construction. |
| `WithZeroMaxBatchSize_ThrowsArgumentOutOfRangeException` | Adapt now | Reject values below 1 before queue construction. |
| `WithCustomHost_DoesNotThrow` | Adapt now | A non-empty Custom host validates and resolves unchanged; HTTP URL normalization can remove only surrounding whitespace and trailing slash. |
| `CanBeSetToAlways` and `CanBeSetToNever` | Port now | Assert `EPostHogPersonProfiles` represents and preserves both values. |
| `CanBeSetToDebug` and `CanBeSetToNone` | Port now | Assert `EPostHogLogLevel` represents and preserves both values. |

Introduce one side-effect-free runtime configuration validation seam used by both tests and subsystem initialization. It may return a boolean plus a diagnostic or a small result type; it should not throw and should not create runtime collaborators. Keep the authoritative editable values in `UPostHogDeveloperSettings`.

Add `UnrealHog/Source/UnrealHog/Private/Tests/PostHogDeveloperSettingsTests.cpp`. Create transient settings objects or a value snapshot so tests never mutate the process-wide default settings object without restoring it.

## Detailed parity matrix: `SdkInfoTests.cs`

The single Unity test is fully relevant.

| Unity test | Unreal disposition | Unreal assertion |
| --- | --- | --- |
| `UserAgent_UsesLibraryNameAndVersion` | Port now | Assert `GetUserAgent()` equals `GetLibraryName() + "/" + GetPluginVersion()`. In an installed plugin test, also assert name and version match `UnrealHog/UnrealHog.uplugin` (`UnrealHog` and `0.1.0` at this baseline). |

The Unreal library name must not be changed to `posthog-unity`; semantic parity is the `<library>/<version>` contract. Add `UnrealHog/Source/UnrealHog/Private/Tests/PostHogSdkInfoTests.cpp`.

## Detailed parity matrix: `FileStorageProviderTests.cs`

The Unity file contains 26 tests. All 26 now have an Unreal equivalent: seventeen cover direct synchronous save/load/delete/state behavior, and nine cover the pending-write/concurrency contract now that `FPostHogFileStorageProvider` performs event file I/O asynchronously on a `UE::Tasks::FPipe`, backed by an in-memory event-ID index for immediate visibility.

### Port or adapt now

| Unity test | Unreal assertion |
| --- | --- |
| `CreatesQueueDirectory` | Construction under a unique test root creates the event queue directory. Account for UnrealHog's SDK namespace directory. Directory casing is a persistence-format decision; lock the chosen spelling in the test and provide migration if existing `Queue`/`State` names change. |
| `CreatesStateDirectory` | Construction creates the state directory. |
| `LoadsExistingEventsFromDisk` | Seed JSON files, construct the provider, and assert `GetEventIds()` returns the extension-free IDs in deterministic lexical order. |
| `AddsEventToIndex_Immediately` | After successful `SaveEvent`, `GetEventIds()` and `GetEventCount()` immediately include the ID, served from the in-memory `EventIdIndex` rather than a disk scan. |
| `WritesEventToDisk_Asynchronously` | `SaveEvent` queues the actual write on a `UE::Tasks::FPipe`; `LoadEvent`/`DeleteEvent` wait for pending writes before touching disk, so the exact JSON text is readable once queued. |
| `MultipleEvents_AllWrittenToDisk` | Save several IDs and assert every file and value. |
| `DuplicateEventId_DoesNotDuplicateInIndex` | Saving one ID twice yields one ID and the documented overwrite result. |
| `NonExistentEvent_ReturnsNull` | Adapt null to Unreal's `false` return and empty out parameter. |
| `ExistingEvent_ReturnsData` | Assert `true` and exact stored content. |
| `RemovesEventFromIndex` | After deletion, enumeration and count no longer contain the ID. |
| `DeletesFileFromDisk` | Assert deletion removes the event file and repeated deletion remains a safe success if that is the chosen idempotent contract. |
| `DeletesAllEventFiles` | `ClearEvents()` removes all event JSON while leaving the queue directory usable. |
| `ReturnsAllEventIds` | Assert all and only the saved IDs are returned in deterministic order. |
| `SaveState_WritesStateFile` | Assert successful save and exact content. |
| `LoadState_ReturnsStoredState` | Assert successful load and exact content. |
| `LoadState_NonExistent_ReturnsNull` | Adapt null to `false` plus an empty out parameter. |
| `DeleteState_RemovesStateFile` | Assert the state file is removed and cannot be loaded. |

Add failure-path coverage required by the Unreal interface even though the Unity methods return `void`: empty event IDs/state keys return `false`, failed loads clear their output, and JSON-object overloads serialize valid objects without changing field types.

### Activated within `FileStorageProviderTests.cs`

Event file I/O now runs on a dedicated `UE::Tasks::FPipe`, so the nine tests previously deferred pending an async/threading contract are implemented in `PostHogFileStorageProviderTests.cpp`:

| Unity test | Unreal test |
| --- | --- |
| `WaitsForPendingWrite_BeforeReading` | `UnrealHog.Storage.FileStorageProvider.WaitsForPendingWriteBeforeReading` |
| `WaitsForPendingWrite_BeforeDeleting` | `UnrealHog.Storage.FileStorageProvider.WaitsForPendingWriteBeforeDeleting` |
| `BlocksUntilAllWritesComplete` | `UnrealHog.Storage.FileStorageProvider.FlushPendingWritesBlocksUntilAllWritesComplete` |
| `WithNoPendingWrites_ReturnsImmediately` | `UnrealHog.Storage.FileStorageProvider.FlushPendingWritesWithNoPendingWritesReturnsImmediately` |
| `CalledMultipleTimes_DoesNotThrow` | `UnrealHog.Storage.FileStorageProvider.FlushPendingWritesCalledMultipleTimesIsIdempotent` |
| `WaitsForPendingWrites_BeforeClearing` | `UnrealHog.Storage.FileStorageProvider.ClearEventsWaitsForPendingWritesBeforeClearing` |
| `ConcurrentSaves_DoNotCorruptIndex` | `UnrealHog.Storage.FileStorageProvider.ConcurrentSavesDoNotCorruptIndex` |
| `ConcurrentSavesAndLoads_DoNotCorrupt` | `UnrealHog.Storage.FileStorageProvider.ConcurrentSavesAndLoadsDoNotCorrupt` |
| `ConcurrentSavesAndDeletes_DoNotThrow` | `UnrealHog.Storage.FileStorageProvider.ConcurrentSavesAndDeletesDoNotThrow` |

`FlushPendingWrites()` is a concrete method on `FPostHogFileStorageProvider`, not on `IPostHogStorageProvider`, matching Unity's `FileStorageProvider.FlushPendingWrites()` being absent from `IStorageProvider`. `GetEventIds()`/`GetEventCount()` are served from the in-memory index rather than a disk scan, so they reflect a just-completed `SaveEvent` immediately without waiting on the async write.

Add `UnrealHog/Source/UnrealHog/Private/Tests/PostHogFileStorageProviderTests.cpp`. Build a small RAII temporary-directory fixture using a unique path under the platform temp directory. Never use the project's real `Saved/UnrealHog` directory in automation.

## Other currently relevant Unity tests

### `UuidV7Tests.cs`

All eight semantic assertions are relevant. Existing `PostHogUuidV7Tests.cpp` already covers canonical shape, version, variant, uniqueness/order under a fixed millisecond, clock rollback, counter rollover, concurrency, and an RFC vector.

Map the byte-oriented Unity assertions to the private packer or parsed `FGuid` fields. Do not add a public `GenerateBytes()` API. Add one production-path smoke test for `PostHogUuidV7::New()` if needed to prove the real clock and entropy wiring produces 1,000 unique, nondecreasing canonical strings. Keep deterministic tests as the primary coverage so failures are reproducible.

### Relevant subset of `JsonSerializerTests.cs`

Port these four tests through `FPostHogEvent::ToJsonObject()`, `FPostHogBatchPayload::ToJsonObject()`, and Unreal's JSON writer:

- `WithBasicEvent_ReturnsValidJson`
- `WithProperties_IncludesProperties`
- `WithEmptyBatch_ReturnsValidJson`
- `WithEvents_IncludesEvents`

Compare the parsed JSON structure, not serialized substring order. Place them in `PostHogEventTests.cpp` or a focused `PostHogPayloadJsonTests.cpp`.

The generic scalar/collection serializer and dictionary deserializer tests are not plugin behavior: UnrealHog delegates JSON primitives to Unreal's `FJsonSerializer` and does not expose a general serializer. Revisit the deserialization subset when persisted event rehydration is implemented.

### Relevant subset of `PostHogSettingsTests.cs`

`UPostHogDeveloperSettings` combines Unity's `PostHogSettings` and `PostHogConfig` layers, so no `ToConfig()` mapping API should be added. Port these semantics instead:

- current-feature defaults match the validated runtime snapshot, with analytics disabled as the repository-required divergence;
- US, EU, and Custom host choices resolve correctly;
- every supported editor setting is a config-backed `UPROPERTY` with the intended edit metadata;
- disabling analytics prevents subsystem creation; and
- empty or whitespace API keys prevent initialization even when analytics was explicitly enabled.

Tests that only prove Unity's ScriptableObject-to-config copy are non-applicable. Assertions for feature flag, exception tracking, and session replay settings should activate with those runtime features, even though placeholder settings fields already exist.

### Relevant subset of `PostHogSDKTests.cs`

Port only behavior reachable through the current runtime subsystem:

- missing configuration does not create a ready subsystem;
- `CaptureEvent` and manual/timer flush are safe no-ops when initialization was denied or failed; and
- denied/unready calls return without creating an event UUID, payload, queue file, or HTTP request.

The feature-flag shutdown reset, `BeforeSend`, identity, alias, group, exception, screen, opt-in/out mutation, and session replay assertions are deferred until those APIs exist. Do not add placeholder public methods just to satisfy the Unity test surface.

## Full Unity test-suite audit

This table accounts for every test file currently present under `PostHog.Unity.Tests`.

| Unity test file | Current disposition | Rationale |
| --- | --- | --- |
| `PostHogEventTests.cs` | Include now | Direct current event behavior; adapt constructor-specific tests. |
| `PostHogConfigTests.cs` | Include now | Direct current settings and validation behavior. |
| `SdkInfoTests.cs` | Include now | Direct current SDK metadata behavior. |
| `FileStorageProviderTests.cs` | Include all 26 tests | Direct current persistence behavior; event I/O is asynchronous via a `UE::Tasks::FPipe`. |
| `UuidV7Tests.cs` | Include now; largely already covered | Direct current UUID utility behavior. |
| `JsonSerializerTests.cs` | Include event/batch subset | Event and batch projection are current; generic serializer/deserializer is not a plugin API. |
| `PostHogSettingsTests.cs` | Include defaults/exposure/gating subset | Unreal uses one `UDeveloperSettings` layer; future-feature fields do not imply implemented behavior. |
| `PostHogSDKTests.cs` | Include initialization/no-op subset | Runtime subsystem exists; most Unity public APIs do not. |
| `SessionManagerTests.cs` | Defer all 4 | UnrealHog has a subsystem-lifetime UUID but no inactivity, background, foreground, maximum-duration, or restart session manager. |
| `NetworkClientTests.cs` | Defer all 11 | Every current test is specifically for feature-flag request retry behavior; feature flags are not implemented. These are not generic batch-upload retry tests. |
| `FeatureFlagModelTests.cs` | Defer all | Feature flag wire models are absent. |
| `FlagCacheTests.cs` | Defer all | Feature flag persistence/cache is absent. |
| `FlagCalledTrackerTests.cs` | Defer all | `$feature_flag_called` tracking is absent. |
| `FlagValueTests.cs` | Defer all | Feature flag value type is absent. |
| `PostHogFeatureFlagTests.cs` | Defer all | Feature flag public API is absent. |
| `PostHogJsonTests.cs` | Defer all | The wrapper supports feature-flag payloads and has no current Unreal counterpart. |
| `LruCacheTests.cs` | Defer all | No current production LRU cache exists; add coverage with the feature that introduces one. |
| `ReplayQueueTests.cs` | Defer all | Session replay queue/compression is absent. |
| `UnityExceptionIntegrationTests.cs` | Defer all | Unity log-handler integration is platform-specific and Unreal exception capture is not implemented. |
| `WebGLExceptionIntegrationTests.cs` | Defer all | WebGL/Unity callback behavior is not an Unreal surface. Add Unreal-specific log/error hooks with exception tracking. |
| `UnityStackTraceParserTests.cs` | Defer all | C# and IL2CPP stack parsing is not relevant to Unreal; future error tracking needs Unreal-native frame tests. |

This audit finds no other test file that should be ported wholesale at the current implementation level.

## Implementation sequence

### 1. Establish shared automation helpers

Create private test helpers only where they reduce repeated risk:

- canonical UUID validation;
- JSON field/type assertions;
- scoped temporary storage directory cleanup; and
- a side-effect-free configuration snapshot/validation seam.

Guard automation files with `WITH_DEV_AUTOMATION_TESTS` and use `EditorContext | ProductFilter`, matching the existing UUID suite. Do not expose helpers from the plugin's public headers.

### 2. Close event, payload, and SDK metadata coverage

Add event model tests, the four relevant JSON payload tests, and the User-Agent test. These are pure or nearly pure and should not require a world, disk, HTTP, or credentials.

### 3. Define and test the configuration boundary

Make analytics disabled by default. Validate a runtime snapshot before UUID or collaborator creation. Cover all 16 `PostHogConfigTests.cs` behaviors using Unreal result semantics, plus US/EU resolution and the no-artifact consent invariant.

Keep editor clamping as user guidance, not as the only correctness boundary; `.ini` values and programmatic mutation can bypass editor widgets.

### 4. Add isolated file storage tests

Port the 17 synchronous cases and interface failure paths using a unique temporary root, plus the nine async/threading cases now that event I/O runs on a `UE::Tasks::FPipe`.

### 5. Add runtime gating integration tests

Provide narrow dependency injection or factories for storage, UUID generation, and HTTP so tests can observe calls without touching disk or network. Cover:

- collection disabled;
- collection enabled with invalid API key;
- valid initialization;
- capture with null and populated properties;
- safe capture/flush when unready; and
- zero downstream artifacts on denied paths.

The seam should improve production isolation and must not become a Blueprint API.

### 6. Maintain the deferral ledger

When a deferred production feature lands, its task must move the associated rows out of this document and into that feature's acceptance tests. In particular:

- session lifecycle activates `SessionManagerTests.cs`;
- feature flags activate their models, cache, value, tracker, JSON wrapper, LRU, and network retry suites;
- exception tracking activates Unreal-native integration and stack-frame coverage; and
- session replay activates replay queue/compression coverage.

## Test execution and verification

### WSL/non-editor checks

Zeroshot workers and validators run in WSL and perform all available non-editor checks:

- inspect the diff and generated-file boundaries;
- verify all new test files are guarded by `WITH_DEV_AUTOMATION_TESTS`;
- search for live PostHog URLs or credentials in fixtures;
- inspect JSON fixtures and temporary-path cleanup;
- confirm every deferred Unity test file remains represented in the audit; and
- run any repository-provided platform-neutral checks.

### Required Zeroshot Unreal verification

From WSL, use the Windows-side Unreal Automation gate:

1. If the required `CI` symlinks are missing from the worktree, run `Scripts/ci-paths.sh` first.
2. Run `Scripts/run-windows-tests.sh`; it builds the Editor target and executes the `UnrealHog.*` automation tests, including the existing UUID tests.
3. Repeat the gate when needed to expose leaked files or order dependence in the file-storage suite.
4. Verify configuration/runtime gating tests use no PostHog credentials, issue no network request, and remove test-owned temporary directories.
5. Record the script output as a required Zeroshot validation gate.

## Acceptance criteria

- Every behavior in `PostHogEventTests.cs`, `PostHogConfigTests.cs`, and `SdkInfoTests.cs` has an Unreal automation assertion or an explicit Unreal API adaptation documented above.
- All 26 behaviors in `FileStorageProviderTests.cs`, including the nine async/thread-safety cases, pass against isolated temporary storage.
- All eight UUIDv7 semantics are covered without exposing a public byte-generation API.
- Event and batch JSON tests validate structure and types without depending on field order.
- Analytics collection defaults to disabled, and denied or invalid initialization produces no identifier, payload, queue record, file, or HTTP request.
- Configuration is validated outside editor metadata before runtime collaborators are created.
- SDK User-Agent is `<UnrealHog library name>/<plugin descriptor version>`.
- No test contacts PostHog or requires credentials.
- Every Unity test file has a disposition in the full-suite audit.
- `Scripts/run-windows-tests.sh` passes and its Unreal Automation output is recorded as a Zeroshot validation gate; run `Scripts/ci-paths.sh` first if the required `CI` symlinks are missing from the worktree.

## Exclusions

- Implementing feature flags, identity/alias/group APIs, session rotation, exception tracking, or session replay.
- Adding asynchronous storage or declaring a new multi-threading contract solely to port Unity's implementation mechanics.
- Reimplementing Unreal's generic JSON serializer.
- Adding Unity-shaped constructors, exceptions, coroutines, ScriptableObjects, or WebGL behavior.
- Calling live PostHog endpoints in acceptance tests.
- Modifying anything under `Docs/Reference/posthog-unity`.

## Smallest useful Unity production references

Use these files to resolve ambiguity while implementing the tests; do not copy them verbatim:

- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/Models/PostHogEvent.cs`
- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/Models/BatchPayload.cs`
- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/PostHogConfig.cs`
- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/PostHogSettings.cs`
- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/Storage/FileStorageProvider.cs`
- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/Utilities/JsonSerializer.cs`
- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/Utilities/SdkInfo.cs`
- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/Utilities/SdkInfo.Generated.cs`
- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/Utilities/UuidV7.cs`
- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/PostHogSDK.cs` for initialization/no-op behavior only
