## 1. Add bounded response parsing to OBCX core

- [x] 1.1 Extend the HTTP test fixture with configurable fixed-length and chunked response bodies, then add failing coverage for the 8 MiB default, a 10 MiB client override, a one-byte-over-limit response, and a zero limit
- [x] 1.2 Add the non-virtual `HttpClient` response-body limit setter/getter and pimpl state with an 8 MiB default
- [x] 1.3 Replace asynchronous direct GET/POST response reads with explicit Beast response parsers that apply the selected finite limit
- [x] 1.4 Apply the same parser policy to deprecated synchronous reads and direct/proxy response-bearing operations without changing timeout, TLS, or decompression behavior
- [x] 1.5 Add or complete direct, proxy, POST, and chunked response-limit tests and run the focused core HTTP suite

## 2. Define the bridge media policy and result contracts

- [x] 2.1 Add `qq_media_download_max_bytes` to immutable bridge configuration with a 10 MiB default and startup/reload validation for the positive 10 MiB maximum
- [x] 2.2 Add configuration tests for the default, a lower valid value, zero, negative input, and an over-maximum candidate
- [x] 2.3 Replace the all-or-nothing image download return type with an index-aligned per-item success/failure outcome and stable failure categories
- [x] 2.4 Introduce prepared-media and media-group fallback result metadata that retain original indices, prior replacement state, multipart usage, and replacement counts

## 3. Implement bounded QQ media downloading

- [x] 3.1 Configure full-media HTTP requests with the actor-generation body limit and `Accept-Encoding: identity`, while leaving range probes on the ordinary core limit
- [x] 3.2 Reject empty and non-image successful responses as item failures and map low-level parser failures to stable bridge categories without retaining complete signed URLs
- [x] 3.3 Replace detached one-coroutine-per-item fan-out with at most three structured workers that preserve input order and are joined before the parent operation returns
- [x] 3.4 Add downloader tests for an 8-to-10 MiB image, known-length and streamed over-limit bodies, out-of-order completion, the concurrency ceiling, and parent cancellation/lifetime cleanup

## 4. Preserve media groups through partial failure

- [x] 4.1 Download and validate the configured placeholder at most once per fallback batch and add a valid embedded PNG for remote-placeholder failure
- [x] 4.2 Substitute failed items at their original indices while retaining all successful bytes and Telegram's 2-to-10 item media-group constraints
- [x] 4.3 Compute replacement totals as a union of sanitization and multipart-fallback indices and propagate structured multipart usage/replacement metadata into captions and logs
- [x] 4.4 Add formatter/actor tests for one and several failed items, placeholder reuse, embedded fallback, no double-counting, preserved order, and successful upload of valid peers
- [x] 4.5 Re-run the existing eleven-image `9 + 2`, multipart cleanup, upload failure, reply/topic, and direct mapping-persistence regressions

## 5. Document and verify the cross-repository change

- [x] 5.1 Document `qq_media_download_max_bytes`, the 10 MiB ceiling, and item replacement behavior in the bridge example configuration and README
- [x] 5.2 Install the updated OBCX SDK into a clean prefix and build/test the standalone bridge against that installed API
- [x] 5.3 Run focused HTTP and bridge suites, then the complete root and actor-package conformance tests
- [x] 5.4 Run `nix fmt` before every repository commit and verify formatting plus whitespace checks in both core and bridge repositories
- [x] 5.5 Run `openspec validate fix-qq-media-download-body-limit --strict --no-interactive` and confirm no user-visible diagnostic includes a signed QQ URL, credential, Boost source path, or complete remote response body
