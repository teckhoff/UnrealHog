# SDKP-007: Add a mockable feature-flag transport

## Status and dependencies

- **State:** Completed
- **Blocked by:** SDKP-002, SDKP-006 (both complete)
- **Blocks:** SDKP-009, SDKP-010, SDKP-012
- **Parity row:** Consent-gated `POST /flags/?v=2`

## Goal

Fetch feature flags through a private, cancellable transport that can be verified without a live PostHog project.

## Required changes

- Add a private request type containing API key, effective distinct ID, optional anonymous ID, groups, person properties, and group properties.
- Send JSON to `POST <canonical-host>/flags/?v=2` with the SDK's JSON headers, User-Agent, and 10-second timeout.
- Add a mockable request/transport seam and cancellation-safe exactly-once completion.
- Honor `FeatureFlagRequestMaxRetries` as retries after the initial attempt, with deterministic exponential delays starting at 300 ms.
- Match the reference retry classification: retry selected status-zero transient connection failures and HTTP 502/504; do not retry other HTTP or data-processing failures.

## Acceptance criteria

- Request JSON omits absent optional fields and uses `$anon_distinct_id`, `$groups`, `person_properties`, and `group_properties` with stable nested JSON types when present.
- A configured retry count of zero sends once; `N` sends at most `N + 1` attempts.
- Reset/timeout/EOF/connection-lost status-zero failures and 502/504 follow the bounded retry path; connection refusal, 408, 429, 500, 503, and malformed-response failures do not.
- Concurrent cancellation or owner destruction cannot deliver a late callback into released state.
- Tests inspect fake requests and fake clock delays and never contact PostHog.
- No request object or payload is created before analytics collection is permitted.
- Run `Scripts/ci-paths.sh` first if required `CI` symlinks are missing, then run `Scripts/run-windows-tests.sh`; record the passing Unreal Automation output as the Zeroshot validation gate.
- When complete, set this task to `Completed` and replace its index status with `✅`.

## Exclusions

- Do not cache responses, expose flag reads, or emit flag-called events.
- Do not reuse `/batch` retry classification where it differs from the reference flag policy.

## Unity references

- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/Core/NetworkClient.cs` (`FetchFeatureFlags`, `CreateFlagsRequest`)
- `Design/Reference/posthog-unity/tests/PostHog.Unity.Tests/NetworkClientTests.cs`

## Validation

Run from WSL against the Windows host project on 2026-07-26:

```
Scripts/run-windows-tests.sh
BUILD RESULT: PASS
AUTOMATION RESULT: 288 passed, 0 passed-with-warnings, 0 failed, 0 not run
AUTOMATION RESULT: PASS
```

Tests added by this row (all passing, none contacting PostHog):

- `UnrealHog.FeatureFlags.Transport.RequestConstruction`
- `UnrealHog.FeatureFlags.Transport.RequestSerialization`
- `UnrealHog.FeatureFlags.Transport.SuccessfulFetchParsesResponse`
- `UnrealHog.FeatureFlags.Transport.MalformedResponseIsTerminal`
- `UnrealHog.FeatureFlags.Transport.RetryBoundsAndDelays`
- `UnrealHog.FeatureFlags.Transport.RetryClassification`
- `UnrealHog.FeatureFlags.Transport.HttpFailureClassification`
- `UnrealHog.FeatureFlags.Transport.StartFailureIsTerminal`
- `UnrealHog.FeatureFlags.Transport.CancellationSafety`
- `UnrealHog.FeatureFlags.Transport.ExactlyOnceCompletion`
- `UnrealHog.Consent.FeatureFlagTransportGate`

## Implementation notes

- Unreal exposes neither the platform error string nor the `CURLcode`, and `EHttpFailureReason` is
  coarser than the reference's classes (`FCurlHttpRequest::FinishRequest`):
  `EHttpFailureReason::ConnectionError` carries `CURLE_COULDNT_CONNECT`,
  `CURLE_COULDNT_RESOLVE_HOST`/`_PROXY` and `CURLE_SSL_CONNECT_ERROR` (terminal for the reference)
  together with `CURLE_OPERATION_TIMEDOUT` and the activity timeout (retryable), while the backend's
  default branch reaches the SDK as `Other`/`None` and mixes the transient mid-exchange drops
  (`CURLE_RECV_ERROR`, `CURLE_PARTIAL_FILE`) with genuinely terminal failures such as certificate
  verification and HTTP/2 protocol errors, plus `CURLE_GOT_NOTHING`, which strikes before any status
  line. `ClassifyHttpFailure` therefore adds three further engine signals: whether the server
  demonstrably started answering, whether request bytes reached the wire, and how long the attempt ran
  against `GetHttpConnectionTimeout()`. The first is taken from the request's own status-line, header,
  and receive-progress callbacks, never from the existence of a response object — the Apple backend
  creates `FAppleHttpResponse` in `FAppleHttpRequest::ProcessRequest`, before any network I/O, and
  keeps it for non-connection failures, so object existence would misclassify terminal Apple TLS and
  protocol failures as retryable. A response that carries a status code, headers, or body bytes counts
  as the same evidence, covering backends that report no progress callbacks. The second comes from
  upload progress: curl only reads the request body once the connection and any TLS handshake have
  succeeded, and `FCurlHttpRequest::FinishRequest` reports the final upload progress before running
  the completion delegate, so "the request was on the wire" is exactly what separates the reference's
  retryable reset/EOF class (`CURLE_RECV_ERROR` or `CURLE_GOT_NOTHING` before any answer) from the
  terminal failures that share the same `Other`/`None` reason (certificate verification, handshake and
  local errors, none of which ever send a byte). A failure once the exchange was under way — request
  sent or response started — is retryable `ConnectionLost`; a `ConnectionError` that never got that
  far is retryable `Timeout` when it consumed the whole connection timeout and terminal
  `ConnectionFailed` otherwise (refusal, DNS and TLS failures return in milliseconds); every other
  unknown failure before the exchange began stays terminal `Other`. The SDK's own 10-second timeout
  (`TimedOut`) is retryable `Timeout`. The status the failed response did carry is preserved, so —
  exactly like the reference's `statusCode != 0` guard — a drop after a real HTTP status is terminal.
  That guard is also what keeps the remaining `Other` failures that do reach the wire terminal: curl
  records the response code as soon as a status line arrives (`FCurlHttpRequest::MarkAsCompleted`), so
  `CURLE_HTTP2_STREAM` and `CURLE_WRITE_ERROR` during a response never retry, and an HTTP/2 handshake
  failure before the request goes out is terminal `Other`.
  `UnrealHog.FeatureFlags.Transport.HttpFailureClassification` covers the mapping end to end through
  a fake `IHttpRequest`, including the connect-timeout, nonzero-status, `CURLE_RECV_ERROR` reset before
  any status, header or body byte, `CURLE_GOT_NOTHING`, post-status HTTP/2 and write errors,
  certificate-failure, and pre-created-empty-response (Apple) cases.
- Cancellation is a barrier in both directions: a completion is claimed and delivered under the
  operation's delivery lock, and attempt creation and start are held under the same lock, so
  `Cancel()` / `CancelAll()` / transport destruction cannot return while a callback is still running
  and cannot return before an attempt that is mid-creation has been started and cancelled. Owners may
  release the state a callback captures, and rely on no further request being issued, as soon as
  cancellation returns. The lock is released before cancellation touches engine handles: cancelling a
  scheduled retry blocks until an executing `FTSTicker` callback returns, and that callback may itself
  be inside the transport waiting for the same lock, so holding it there would deadlock. Marking the
  operation finished under the barrier is what makes the release safe — the timer callback and any
  attempt completion can then only observe a finished operation and return.
  `UnrealHog.FeatureFlags.Transport.CancellationSafety` proves all three races with a blocking attempt
  factory, a blocking completion callback, and a retry clock that reproduces `RemoveTicker`'s waiting
  semantics against a firing timer.
- `Fetch()` returns a null handle when the fetch already completed synchronously (serialization or
  request-start failure), matching the documented contract; the outcome is always reported through
  the callback first.
- Evaluation properties (`person_properties` / `group_properties`) are modelled and serialized here
  but are not yet populated by the consent controller; SDKP-010 supplies them.
