# CI/

This folder holds **machine-local symlinks** used by the Windows test gate.
Everything here except this README is untracked and created by local git
hooks (`post-checkout` / `post-merge`) on machines configured to run the
gate. On other machines this folder is empty — that is expected.

| Link | Points to | Purpose |
| --- | --- | --- |
| `UnrealEngine` | Local Unreal Engine 5.8 install | Editor binary for automation tests; targeted header lookups only |
| `HostProject` | Windows-side staging host project | Where plugins are synced and tests execute |
| `Reports` | Windows-side report directory | Automation report output (`index.json`, editor logs) |

## Rules for agents

- Never modify, create, or delete anything under `CI/`.
- Never search `CI/` recursively — `UnrealEngine` resolves to millions of
  lines of engine source. Use explicit paths for targeted lookups
  (e.g. `CI/UnrealEngine/Engine/Source/Runtime/Online/HTTP/Public/`).
- Test evidence is the output of `Scripts/run-windows-tests.sh` (the
  parsed `AUTOMATION RESULT` lines), **not** the raw editor log under
  `CI/Reports/`.
- If a link is missing, run the local post-checkout hook or set the env
  overrides documented in `Scripts/run-windows-tests.sh`. A missing link
  is an environment problem (gate exit code 2), not a code problem.

## One-time machine setup

See the repository setup notes: create the minimal Windows host project,
ensure the engine drive is mounted in WSL, install the local hooks, and
run the hook once.
