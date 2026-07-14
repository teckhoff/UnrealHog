# EP-014: Add manual exception event capture

## Status and dependencies

- **State:** Blocked
- **Blocked by:** EP-003, EP-004, EP-007
- **Blocks:** EP-015, EP-029
- **Parity row:** Manual `$exception` event ingress

## Goal

Convert an explicitly supplied Unreal error/stack representation into the PostHog exception event schema and send it through standard capture.

## Required changes

- Define an idiomatic public exception input struct containing message, type, stack text or frames, handled state, and optional properties.
- Build the `$exception_list` nested payload and required exception properties in a private builder.
- Add Blueprint/C++ `CaptureException` and route it through the shared capture path.
- Attach the effective distinct ID once through normal event composition.

## Acceptance criteria

- Valid handled exceptions emit one `$exception` event with stable nested JSON types.
- Missing required exception input is a safe no-op; optional properties cannot replace SDK exception fields.
- Multiline and non-ASCII messages/stacks serialize correctly.
- Consent, profile, session, before-send, persistence, and retry behavior are inherited rather than reimplemented.
- Windows Automation uses fixtures and no deliberate process crash.

## Exclusions

- Do not hook Unreal error delegates; EP-015 owns automatic capture.
- Do not copy Unity stack parser implementation or invent cross-language frame fields unsupported by PostHog.

## Unity references

- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/ErrorTracking/ExceptionManager.cs`
- `Docs/Reference/posthog-unity/com.posthog.unity/Runtime/ErrorTracking/ExceptionPropertiesBuilder.cs`
