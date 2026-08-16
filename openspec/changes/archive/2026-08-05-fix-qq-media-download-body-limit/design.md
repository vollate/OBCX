## Context

The bridge first asks Telegram to fetch QQ image URLs. A Telegram `400 Bad Request` enters the multipart fallback, where `ImageUrlValidator::download()` downloads every effective URL and passes the bytes to the Telegram uploader. That downloader creates an OBCX `HttpClient`, whose convenience `http::async_read()` call constructs a Beast response parser with the library's 8 MiB default body limit. A larger declared `Content-Length` therefore fails during header parsing.

The fallback downloads up to ten items concurrently and stores optional results, but its public contract returns only complete images. After all downloads finish it throws at the first missing index, so one oversized or expired item drops the complete batch. The one-byte reachability probe does not prevent this: a server can honor the probe's range and still expose a full body larger than the fallback reader allows.

OBCX core owns direct/proxy HTTP behavior and is installed as the SDK used by the standalone bridge actor. The fix therefore crosses the core HTTP API and bridge package boundary. It must retain finite limits because QQ URLs are external input, and it must remain compatible with Telegram media groups of at most ten items.

## Goals / Non-Goals

**Goals:**

- Preserve an 8 MiB default for ordinary OBCX HTTP responses.
- Let a caller select a finite response-body limit without replacing the core HTTP client.
- Accept QQ photos up to the public Telegram photo ceiling of 10 MiB.
- Isolate download failures by media index and preserve every valid peer in the batch.
- Preserve source order, replacement counts, and existing `9 + 2` splitting when eleven items remain.
- Bound active download concurrency and ensure child work is joined before the actor invocation returns.
- Convert low-level Beast failures into stable bridge diagnostics that do not disclose signed URLs.

**Non-Goals:**

- Uploading an original photo larger than 10 MiB.
- Resizing or transcoding oversized images in this change.
- Removing response-body limits or raising the default globally.
- Replacing the existing in-memory Telegram multipart request builder with a streaming implementation.
- Changing the set of Telegram `400` responses that trigger multipart fallback.

## Decisions

### 1. OBCX exposes a finite per-client response-body limit

`HttpClient` will expose a non-virtual, pimpl-backed setter and getter for a positive response-body limit. The pimpl initializes it to `8 * 1024 * 1024`, preserving current behavior without changing the class size or virtual interface used by consumers.

Every response-bearing direct/proxy and asynchronous/synchronous path will use an explicit `http::response_parser<http::string_body>`, set its `body_limit`, read the response, and release the parsed message. GET and POST use the configured limit; HEAD remains bounded and does not opt into unlimited body parsing. Zero is rejected rather than interpreted as unlimited.

A per-request overload was considered, but adding options to every virtual GET/POST signature would broaden the source and override migration. An actor-local Beast downloader was rejected because it would duplicate TLS, timeout, proxy, compression, and error behavior already owned by core. Raising Beast's effective default globally was rejected because API JSON and polling responses do not need the larger memory allowance.

### 2. Bridge uses an immutable QQ media limit of at most 10 MiB

`BridgeConfig` will add `qq_media_download_max_bytes`, defaulting to `10 * 1024 * 1024`. Configuration loading accepts positive values no larger than 10 MiB and rejects an invalid candidate generation during startup or reload. Operators can lower the limit for a constrained deployment, but increasing it beyond Telegram's public photo ceiling is not supported by this change.

The full-media request sets the new `HttpClient` limit and sends `Accept-Encoding: identity`, making the configured bound apply directly to the media bytes rather than to a compressed representation that could expand after parsing. The one-byte range probe retains the ordinary core limit. Empty or non-image responses are classified as item failures even if their HTTP status is successful.

A fixed 10 MiB core default was rejected because it would affect unrelated traffic. An unlimited or 50 MiB bridge setting was rejected because a ten-item group and the current in-memory multipart builder would permit excessive process memory without improving compatibility with the public Telegram photo API.

### 3. Downloading returns one structured outcome per input item

The bridge downloader will return an order-preserving vector whose length always equals the input length. Each element contains either `DownloadedImage` or a stable failure category and diagnostic detail. It will not throw merely because one item failed; invocation-level errors remain reserved for failures that prevent the operation itself from producing outcomes.

At most three worker coroutines will download media at once. Workers claim indices from shared bounded state and the parent awaits every worker through structured completion; workers do not use `asio::detached` and do not retain references to actor-generation state after return. Results are assembled by original index, not completion order.

The existing fan-out of one detached coroutine per item was rejected because it maximizes simultaneous response buffers and complicates cancellation/lifetime reasoning. Sequential downloading was rejected because one slow URL would unnecessarily serialize all ten items.

### 4. Multipart fallback substitutes failed items before upload

The prepared-media representation will retain each item's original index and whether URL sanitization already replaced it. On multipart fallback:

1. Keep each successful download at its original index.
2. For every failed download, obtain the configured placeholder once per batch and reuse its bytes.
3. If that placeholder cannot be downloaded or validated, use an embedded small PNG that has no network dependency.
4. Upload the resulting group in the original order.

Replacement accounting is the union of item indices replaced during URL sanitization and multipart download, so one item is never counted twice. The structured fallback result carries the Telegram response, whether multipart was used, and the replacement count. Captions and logs use that result rather than reconstructing status from exception strings.

Dropping failed items was rejected because it can turn a valid group into a one-item invalid media group and shifts the text's `[图片N]` references. Throwing on the first failed item was rejected because it preserves the current batch-loss defect. Downloading the remote placeholder independently for every failure was rejected because it adds redundant traffic and another correlated failure source.

### 5. Limits and failures remain observable without leaking URLs

Core continues to throw `HttpClientError` when the parser rejects a response. The bridge maps transport, size-limit, empty-body, and invalid-image failures to stable item categories before constructing user-visible text. User-visible batch errors and captions include batch/index or replacement counts but not complete QQ URLs, Telegram credentials, Boost source paths, or complete remote response bodies.

Logs record the category, item index, configured limit, and whether placeholder recovery succeeded. This provides enough information to verify the rollout while avoiding signed URL disclosure.

## Risks / Trade-offs

- **[A QQ image exceeds 10 MiB]** -> Replace that item and preserve the batch; resizing/transcoding remains a future enhancement.
- **[Ten near-limit files create a large multipart request]** -> Keep the 10 MiB per-item maximum, three active downloads, and Telegram's ten-item batch maximum; streaming multipart remains future work.
- **[A custom placeholder URL is unavailable]** -> Download it once and fall back to an embedded PNG.
- **[The core API is present in source but absent from the installed SDK used by bridge]** -> Install and test core first, then perform a standalone bridge build against that clean prefix.
- **[Direct and proxy readers diverge]** -> Apply one documented setting to all response parsers and cover both paths with focused tests.
- **[Cancellation leaves untracked work]** -> Use structured worker completion and test that no download worker survives the parent operation.

## Migration Plan

1. Add the core response-limit API and parser tests while retaining the 8 MiB default.
2. Install the updated OBCX SDK into a clean prefix.
3. Add bridge configuration, structured item outcomes, bounded workers, and placeholder recovery against that SDK.
4. Run focused HTTP/bridge tests, the complete root suite, and the standalone installed-SDK bridge build.
5. Deploy with the default 10 MiB bridge policy and monitor limit, replacement, and multipart success categories.

No database migration is required. Rollback can restore the prior bridge behavior independently because the core client's default remains 8 MiB and the new setter is opt-in.

## Open Questions

None. Preserving originals larger than 10 MiB requires a separate conversion or document-upload proposal.
