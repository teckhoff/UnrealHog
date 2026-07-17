# UNREAL-???: Task Title

## Status and dependencies

- **State:** Blocked/Ready/Completed
- **Blocked by:** None
- **Blocks:** None

## Goal



## Required changes



## Acceptance criteria

- Run `Scripts/ci-paths.sh` first if the required `CI` symlinks are missing from the worktree, then run `Scripts/run-windows-tests.sh`; the Unreal Automation tests must pass and their output must be recorded as a Zeroshot validation gate.
- Do NOT modify any testing scripts in response to failed test. Make a note of the failed test to be deferred to a human. You may still modify non-test project files in response to failed tests.
- When this task is completed, update its **State** to `Completed` and replace its status icon in `Docs/Planning/MakeItUnreal/README.md` with `✅`.

## Exclusions