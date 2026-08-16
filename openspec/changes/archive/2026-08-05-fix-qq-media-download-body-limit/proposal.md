## Why

QQ-to-Telegram media-group fallback downloads use Boost.Beast's implicit 8 MiB response limit, so reachable QQ images larger than 8 MiB fail with `body limit exceeded`. The downloader then abandons the entire 2-to-10 item batch when any one item fails, causing otherwise valid forwarded images to be lost.

## What Changes

- Add an explicit, finite per-client HTTP response-body limit while preserving the existing 8 MiB default for ordinary OBCX traffic.
- Give bridge QQ media downloads a validated actor-generation size policy, defaulting to 10 MiB for public Telegram photo uploads.
- Return order-preserving per-item download outcomes instead of failing the whole download operation at the first bad item.
- Replace unavailable, oversized, or invalid fallback items with a placeholder while retaining valid items and Telegram media-group cardinality.
- Bound media-download concurrency and avoid exposing Boost source locations or signed QQ URLs in user-facing errors.
- Add focused core, bridge configuration, partial-failure, and batch-preservation tests.

## Capabilities

### New Capabilities

- `http-response-body-limits`: Finite default and caller-selected response-body limits for direct and proxy OBCX HTTP clients.
- `bridge-qq-media-fallback`: Bounded QQ media downloading, item-level recovery, placeholder substitution, and batch-preserving multipart fallback to Telegram.

### Modified Capabilities

None.

## Impact

- OBCX `HttpClient` public configuration API and its asynchronous, synchronous, direct, and proxy response readers.
- `obcx-actor-bridge` immutable configuration, QQ image downloader, media-group fallback result contract, logging, and tests.
- The bridge example configuration and operational documentation.
- The standalone bridge must build against an installed SDK containing the new HTTP response-limit API; no database schema or stored-data migration is required.
