# SDKP-002: Normalize all trailing Custom-host slashes

## Status and dependencies

- **State:** Completed
- **Blocked by:** None
- **Blocks:** SDKP-007, SDKP-014
- **Parity row:** Canonical host resolution for every HTTP endpoint

## Goal

Resolve US, EU, and Custom hosts to one canonical base URL so endpoint builders cannot create accidental double slashes.

## Required changes

- Trim surrounding whitespace and every trailing `/` at the validated settings boundary.
- Keep the repository-mandated US and EU hosts unchanged.
- Make downstream batch, flags, and replay URL builders consume the canonical host; retain defensive normalization only if it is idempotent.
- Add table-driven settings and URL tests for zero, one, and multiple trailing slashes.

## Acceptance criteria

- `https://example.com`, `https://example.com/`, and `https://example.com///` all resolve to `https://example.com`.
- Endpoint construction yields exactly `/batch`, `/flags/?v=2`, or `/s/` after the canonical host.
- Blank Custom-host fallback and invalid-configuration behavior remain unchanged.
- Whitespace inside a nonblank URL is not silently rewritten into a different host.
- Run `Scripts/ci-paths.sh` first if required `CI` symlinks are missing, then run `Scripts/run-windows-tests.sh`; record the passing Unreal Automation output as the Zeroshot validation gate.
- When complete, set this task to `Completed` and replace its index status with `✅`.

## Exclusions

- Do not introduce general URL parsing, DNS validation, or live connectivity checks.
- Do not change the supported host choices or regional defaults.

## Unity references

- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/Core/NetworkClient.cs` (`TrimEnd('/')` and endpoint construction)
- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/SessionReplay/ReplayQueue.cs`
