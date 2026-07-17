# UNREAL-002: UnrealHog Gameplay Tags Companion Plugin

## Status and dependencies

- **State:** Ready
- **Blocked by:** None
- **Blocks:** None

## Goal

Create a plugin that exposes additional API surface to allow developers to define PostHog events and fire them off using GameplayTags instead of just strings.

## Required changes

- Manage a separate plugin to keep GameplayTag dependency away from the core of UnrealHog.
- Provide a way for users to define their own `PostHog.Event.` gameplay tags, and map it to an event string that will be sent with the event.
- Create a Blueprint function library to expose new Blueprint API for calling the PostHogRuntimeSubsytem's CaptureEvent function using a gameplay tag that resolves into the given string.

## Acceptance criteria

- Run `Scripts/ci-paths.sh` first if the required `CI` symlinks are missing from the worktree, then run `Scripts/run-windows-tests.sh`; the Unreal Automation tests must pass and their output must be recorded as a Zeroshot validation gate.
- Do NOT modify any testing scripts in response to failed test. Make a note of the failed test to be deferred to a human. You may still modify non-test project files in response to failed tests.
- When this task is completed, update its **State** to `Completed` and replace its status icon in `Docs/Planning/MakeItUnreal/README.md` with `✅`.

## Exclusions