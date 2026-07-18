# Repository Guidelines

## Project Scope

This project reimplements PostHog's C# Unity SDK as an idiomatic Unreal Engine 5.8 C++ plugin. `Design/Reference/posthog-unity` is the behavioral reference; preserve observable SDK behavior without mechanically translating Unity implementation details.

## Repository Structure

- `UnrealHog/` contains the SDK plugin. Its nested `AGENTS.md` defines plugin-specific conventions.
- `Design/` contains relevant markdown files for architecture and planning, as well as a reference to the Unity SDK for parity matching.
- `Design/Reference/posthog-unity` contains the existing Unity SDK.
- `CI/UnrealEngine` is an optional machine-local, gitignored symlink to an Unreal Engine 5.8 source checkout (read-only; may be absent on some machines). It sits on a slow Windows mount and repo-wide searches do not traverse the symlink, so when consulting engine source, target searches explicitly at narrow subtrees such as `CI/UnrealEngine/Engine/Source/Runtime/...`.

Use repository-relative paths in documentation, tasks, scripts, issues, and pull requests. Never commit machine-specific checkout paths.

If `Design/Reference/posthog-unity` does not exist, pull the submodule using `git submodule update --init` before performing any work.

If working in a git worktree, run `Scripts/link-engine-source.sh` before consulting engine source; it recreates the gitignored `CI/UnrealEngine` symlink by mirroring it from the main checkout, and is a safe no-op when the symlink is absent there.

Everything under `Design/Reference/posthog-unity` is read-only third-party reference material; never modify, move, or reformat it, and never copy it verbatim.

`CI/UnrealEngine` is a symlink to the local engine source for targeted header lookups only. Do NOT edit any files in this folder. Search it with explicit paths, never recursively.

## Architecture Invariants

- Project configuration lives in `UDeveloperSettings`. The supported server choices are `US`, `EU`, and `Custom`; US resolves to `https://us.i.posthog.com` and EU resolves to `https://eu.i.posthog.com`.
- Runtime SDK orchestration lives in a `UGameInstanceSubsystem` with idiomatic Blueprint and C++ APIs.
- Analytics collection is opt-in by default. No event payload, queue record, file, or HTTP request may be created before collection is permitted.
- Keep tests independent of live PostHog credentials. Isolate or mock HTTP for normal acceptance tests.

## Coding and Testing Conventions

Follow Unreal C++ conventions: tabs, `U`/`A`/`F`/`I` prefixes, PascalCase types and functions, and `b` prefixes for booleans. Prefer focused automation tests for payload construction, consent gating, host resolution, identity/session behavior, persistence, and retry policy.

Every change should be traceable to a scoped task with explicit acceptance criteria, exclusions, relevant parity rows, and direct pointers to the smallest useful set of Unity reference files. Keep unrelated parity work out of the same change.

## Build and Verification Environment

Zeroshot workers and validators run in WSL. In that environment, perform all available non-editor verification: inspect diffs and generated-file boundaries, check header/module conventions, validate fixtures and JSON logic with runnable isolated tests, and run any repository-provided platform-neutral checks.

Zeroshot validators are able to perform automation testing using the Unreal Automation Tool. To perform this testing:
- If the required `CI` symlinks do not exist in the worktree, run `Scripts/ci-paths.sh` first.
- Run `Scripts/run-windows-tests.sh`
- Record and verify the output as a validation gate for Zeroshot.

## Commits, Issues, and Pull Requests

Use concise imperative commit subjects. Pull requests should summarize behavior changes, tests and their execution environment, linked issues, the affected parity row, and screenshots when editor settings or Blueprint UI changes.

Agents must open normal pull requests against `dev`, including when the repository's default branch is `main`. With `gh`, pass `--base dev` explicitly instead of relying on the repository default. Never open a feature or maintenance pull request against `main`. The only permitted pull request into `main` is a milestone promotion from this repository's `dev` branch, and an agent must not create that promotion unless the user explicitly requests it.

Every issue or pull request created by an agent must include this notice prominently in its body:

> This issue or pull request was created by an automated coding agent, not a human.

If possible, also include the model of the agent running.

### Pull request completion contract

Creating or updating a pull request is incomplete until all of these conditions hold:

1. Set the pull request base to `dev` and verify the published base branch. The only exception is an explicitly requested milestone promotion from `dev` to `main`.
2. Read `.github/pull_request_template.md` immediately before composing the pull request body.
3. Preserve every required heading from the template, in the same order.
4. Fill every section with reviewer-facing content. Do not submit empty sections or template instructions as answers.
5. Include the automated-agent notice above verbatim.
6. Under `How did you test this code?`, include the command, environment, actual result, and relevant output from `Scripts/run-windows-tests.sh`. Never claim a test that was not run.
7. Open new pull requests as drafts.
8. After creating or editing the pull request, retrieve its published base, title, and body from GitHub. Confirm `baseRefName` is `dev` and the published metadata meets the requirements above.
9. If the published metadata does not meet those requirements, update the pull request and check it again. Do not report completion until it complies.

Do not use `gh pr create --fill`, generated commit summaries, or an independently invented body as a substitute for the repository template.

Reviewers must inspect both the patch and the published pull request metadata. Reject a normal pull request unless its base branch is `dev`; reject a pull request into `main` unless it is an explicitly requested milestone promotion from this repository's `dev` branch. Also reject the result if the title or body omits or reorders a template section, leaves a section empty, lacks the automated-agent notice, or does not record the actual Unreal Automation Tool result. Do not approve based only on code correctness.

## Task Protocol
Work only within the scope of the assigned pull request or task file. If the task conflicts with this document, stop and surface the conflict in the PR body rather than resolving it silently.
