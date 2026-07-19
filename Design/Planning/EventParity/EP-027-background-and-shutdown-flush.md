# EP-027: Flush safely on background and shutdown

## Status and dependencies

- **State:** Completed
- **Blocked by:** EP-002, EP-016, EP-022, EP-026
- **Blocks:** EP-029
- **Parity row:** Background persistence and `FlushOnQuit`

## Goal

Preserve queued data when the application is suspended and attempt a bounded final network drain when it quits.

## Design notes

The Unity SDK never flushes during engine teardown: `Application.wantsToQuit` is a veto hook, so `LifecycleHandler.OnWantsToQuit` cancels the quit while the engine is still alive, runs the flush coroutine against a timeout, then calls `Application.Quit()` itself. The Unreal design must mirror that shape, because once Unreal shutdown begins the flush is impossible by construction: `FHttpManager::Flush(EHttpFlushReason::Shutdown)` sets `bFlushing`, which refuses new HTTP requests, and the main loop stops ticking, so completion delegates either never fire or fire into partially destroyed objects. `OnEnginePreExit` and subsystem `Deinitialize` therefore must never initiate network I/O — prior attempts to do so crashed.

The Unreal analog of `Application.wantsToQuit` is `UGameViewportClient::OnWindowCloseRequested()`: a bindable delegate whose handler can return `false` to veto the close. Desktop window close (the X button) and Alt-F4 both route through it via `FSceneViewport`. It is a single-bind delegate, not multicast, so SDK binding can conflict with game code that also uses it — gate the binding strictly behind `bFlushOnQuit` and document the conflict.

Programmatic quits (`UKismetSystemLibrary::QuitGame`, the `quit` console command) call `RequestEngineExit` directly and bypass the window-close veto, so the SDK must also expose an explicit flush-and-quit entry point for developers to call instead.

Network delivery at quit is a latency optimization, not the durability mechanism. Durability comes from the EP-019 storage-authoritative queue plus EP-018 rehydration: anything not delivered before exit — including task-manager kills, crashes, and mobile process death — is sent on next launch.

## Required changes

- On background (`FCoreDelegates::ApplicationWillEnterBackgroundDelegate`), stop session activity, request an asynchronous best-effort flush, and synchronously drain pending event file writes before suspension. The engine is alive at this point so starting requests is safe, but on iOS the process is suspended moments later, so delivery is best-effort only; the synchronous storage drain is the guarantee.
- When `bFlushOnQuit` is enabled, bind `UGameViewportClient::OnWindowCloseRequested()` to veto the desktop window close, run the EP-022 drain under a ticker-driven timeout of up to `FlushOnQuitTimeoutSeconds` while the engine still ticks normally, then request engine exit on completion or timeout.
- Expose a flush-and-quit API (Blueprint and C++) on the subsystem that performs the same bounded drain and then requests engine exit, covering programmatic quit paths that bypass the window-close veto.
- On timeout, retain unsent records, cancel safely, drain pending storage writes, and allow quit.
- Never initiate network requests from `Deinitialize`, `OnEnginePreExit`, or `FCoreDelegates::ApplicationWillTerminateDelegate`; those paths perform synchronous storage drain only.
- When disabled, skip the network wait but still ensure queued writes are durable before collaborator destruction.
- Make deinitialization idempotent after either normal completion or timeout, and finalize once even when the vetoed close, the flush-and-quit API, and engine shutdown overlap.

## Acceptance criteria

- Background and quit signals never delete unsent events merely because a request is cancelled.
- Successful final flush drains every batch before completion.
- The final drain runs only while the engine is still ticking (vetoed window close or the flush-and-quit API); `Deinitialize`, `OnEnginePreExit`, and terminate paths perform storage drain only and issue no network requests.
- The window-close veto is bound only when `bFlushOnQuit` is enabled, and engine exit is requested exactly once after drain completion or timeout.
- Timeout permits shutdown with unsent files recoverable by EP-019 on next launch.
- Repeated/overlapping lifecycle signals invoke finalization once.
- Tests use fake lifecycle signals, transport, clock/timer, and storage drain counters; Windows verifies the real delegate wiring.
- Run `Scripts/ci-paths.sh` first if the required `CI` symlinks are missing from the worktree, then run `Scripts/run-windows-tests.sh`; the Unreal Automation tests must pass and their output must be recorded as a Zeroshot validation gate.
- When this task is completed, update its **State** to `Completed` and replace its status icon in `Design/Planning/EventParity/README.md` with `✅`.

## Exclusions

- Do not guarantee network delivery after the configured timeout.
- Do not guarantee network delivery for quit paths that bypass the window-close veto (process kill, crash, `RequestEngineExit` without the flush-and-quit API) or for mobile backgrounding; EP-018/EP-019 persistence delivers those events on next launch.
- Do not attempt network I/O during engine teardown under any configuration.

## Unity references

- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/Core/LifecycleHandler.cs` (`OnWantsToQuit`)
- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/PostHogSDK.cs` (`OnAppBackground`, `OnAppQuit`)
