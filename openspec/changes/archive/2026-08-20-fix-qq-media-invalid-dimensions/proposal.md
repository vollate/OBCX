## Why

QQ merged-forward albums can contain valid JPEG files whose dimensions violate Telegram's photo constraints. The current fallback validates size and image signatures but not dimensions, so one overlong photo can make Telegram reject the final atomic multipart group with `PHOTO_INVALID_DIMENSIONS`, discarding every otherwise valid peer.

## What Changes

- Inspect each downloaded fallback image's dimensions before constructing the Telegram multipart request.
- Normalize an otherwise valid photo that exceeds Telegram's supported dimension sum or aspect-ratio limits by bounded, aspect-preserving downscaling.
- Treat undecodable images, unsafe decode sizes, and failed normalization as per-item failures, replacing only those positions with the existing placeholder while preserving valid peers and media order.
- Validate normalized output before upload and report stable per-item categories and aggregate replacement/normalization counts without exposing signed URLs or full remote responses.
- Cover ordinary QQ albums and images extracted from merged-forward messages, including a regression case for a 2048×13301 JPEG.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `bridge-qq-media-fallback`: Extend full-image acceptance and per-item fallback semantics to enforce Telegram photo-dimension constraints and normalize recoverable over-dimension photos before atomic multipart upload.

## Impact

- Affects QQ media downloading, validation, temporary multipart materialization, fallback accounting, and bridge diagnostics under `local_actor/obcx-actor-bridge`.
- May use the existing configured blocking executor and media tooling for bounded image inspection and transformation; no Telegram or QQ API changes are required.
- Adds CPU and temporary-storage work only on the multipart fallback path and only for images requiring normalization.
- Existing bridge configuration remains compatible; no breaking changes are introduced.
