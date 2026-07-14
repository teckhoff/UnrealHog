# Repository Guidelines

## Project Scope

This project reimplements PostHog's C# Unity SDK as an idiomatic Unreal Engine 5.8 C++ plugin. `Docs/Reference/posthog-unity` is the behavioral reference; preserve observable SDK behavior without mechanically translating Unity implementation details.

## Repository Structure

- `UnrealHog/` contains the SDK plugin. Its nested `AGENTS.md` defines plugin-specific conventions.
- `Docs/` contains relevant markdown files for architecture and planning, as well as a reference to the Unity SDK for parity matching.
- `Docs/Reference/posthog-unity` contains the existing Unity SDK.
- `CI/UnrealEngine` is an optional machine-local, gitignored symlink to an Unreal Engine 5.8 source checkout (read-only; may be absent on some machines). It sits on a slow Windows mount and repo-wide searches do not traverse the symlink, so when consulting engine source, target searches explicitly at narrow subtrees such as `CI/UnrealEngine/Engine/Source/Runtime/...`.

Use repository-relative paths in documentation, tasks, scripts, issues, and pull requests. Never commit machine-specific checkout paths.

If `Docs/Reference/posthog-unity` does not exist, pull the submodule using `git submodule update --init` before performing any work.

If working in a git worktree, run `Scripts/link-engine-source.sh` before consulting engine source; it recreates the gitignored `CI/UnrealEngine` symlink by mirroring it from the main checkout, and is a safe no-op when the symlink is absent there.

Everything under `Docs/Reference/posthog-unity` is read-only third-party reference material; never modify, move, or reformat it, and never copy it verbatim.

`CI/UnrealEngine` is a symlink to the local engine source for targeted header lookups only. Search it with explicit paths, never recursively.

## Architecture Invariants

- Project configuration lives in `UDeveloperSettings`. The supported server choices are `US`, `EU`, and `Custom`; US resolves to `https://us.i.posthog.com` and EU resolves to `https://eu.i.posthog.com`.
- Runtime SDK orchestration lives in a `UGameInstanceSubsystem` with idiomatic Blueprint and C++ APIs.
- Analytics collection is opt-in by default. No event payload, queue record, file, or HTTP request may be created before collection is permitted.
- Keep tests independent of live PostHog credentials. Isolate or mock HTTP for normal acceptance tests.

## Coding and Testing Conventions

Follow Unreal C++ conventions: tabs, `U`/`A`/`F`/`I` prefixes, PascalCase types and functions, and `b` prefixes for booleans. Prefer focused automation tests for payload construction, consent gating, host resolution, identity/session behavior, persistence, and retry policy.

Every change should be traceable to a scoped task with explicit acceptance criteria, exclusions, relevant parity rows, and direct pointers to the smallest useful set of Unity reference files. Keep unrelated parity work out of the same change.

## Build and Verification Environment

Builds and automation tests are required to be done on Windows.

Zeroshot workers and validators run in WSL and must not attempt to invoke Windows Unreal executables. In that environment, perform all available non-editor verification: inspect diffs and generated-file boundaries, check header/module conventions, validate fixtures and JSON logic with runnable isolated tests, and run any repository-provided platform-neutral checks. Record the Windows build and Automation suite as required manual or external-CI verification when those checks cannot run locally.

Do not configure an agent quality gate that requires an Unreal build until a self-hosted Windows CI runner exists. Once available, the gate should query that external CI status rather than launching Unreal from WSL.

## Commits, Issues, and Pull Requests

Use concise imperative commit subjects. Pull requests should summarize behavior changes, tests and their execution environment, linked issues, the affected parity row, and screenshots when editor settings or Blueprint UI changes.

Every issue or pull request created by an agent must include this notice prominently in its body:

> This issue or pull request was created by an automated coding agent, not a human.

If possible, also include the model of the agent running.

Use the pull request table located in `.github/pull_request_template.md`.

## Task Protocol
Work only within the scope of the assigned pull request or task file. If the task conflicts with this document, stop and surface the conflict in the PR body rather than resolving it silently.
