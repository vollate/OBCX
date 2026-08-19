## Context

The QQ multipart fallback currently accepts a full response when it is non-empty, below `qq_media_download_max_bytes`, and has a recognized image signature. It then uploads every accepted item as a Telegram `photo`. Telegram applies additional constraints: width plus height must not exceed 10,000 pixels, and the ratio between the larger and smaller dimensions must not exceed 20. Because `sendMediaGroup` is atomic, one violating item causes every valid peer to be rejected.

The observed regression was a valid 7.1 MiB JPEG measuring 2048×13301. The signature and byte-size checks accepted it (`replaced=0/3`), but Telegram rejected the multipart request with `PHOTO_INVALID_DIMENSIONS`; the accompanying 1024×372 and 960×960 images were consequently lost.

The existing bridge already has a finite 10 MiB download policy, an actor-generation `ffmpeg_path`, a process-owned blocking executor, temporary-file cleanup, per-item fallback outcomes, and stable diagnostics. The new work must preserve those bounds and must not expose signed QQ URLs or decoder output.

## Goals / Non-Goals

**Goals:**

- Detect Telegram-incompatible photo dimensions before multipart upload.
- Preserve recoverable overlong photos by producing a compliant, aspect-preserving representation.
- Preserve all valid peers and replace only an item that cannot be safely inspected or normalized.
- Keep decoding, transformation concurrency, output size, execution time, and temporary storage finite.
- Make normalization and replacement decisions observable through stable, non-sensitive categories and counts.

**Non-Goals:**

- Changing Telegram's direct remote-URL behavior before that path fails.
- Upscaling small images, enhancing image quality, or providing operator-selectable resize styles.
- Preserving animation when an animated source is being sent through a Telegram `photo` item.
- Changing QQ/TG mapping, retry-queue semantics, or Telegram's atomic media-group API.
- Replacing the existing 10 MiB media download ceiling.

## Decisions

### 1. Inspect dimensions from bounded encoded headers before invoking a decoder

Add a QQ fallback photo inspector that reads dimensions from the already bounded in-memory body. It will support the image families currently recognized by the downloader (JPEG, PNG, GIF, WebP, and BMP), use checked integer arithmetic, reject zero or truncated dimensions, and return a stable result rather than decoder text.

A photo is Telegram-compliant when both dimensions are positive, `width + height <= 10000`, and `max(width, height) / min(width, height) <= 20`. Compliant items retain their original encoded bytes; they are not re-encoded merely because multipart fallback was selected.

This is preferred over launching `ffprobe` for every item because header inspection avoids extra processes and avoids decoding valid images. It is also preferred over decoding every body in-process because an encoded byte limit alone does not bound decoded memory.

### 2. Normalize only recoverable dimension violations

For a supported photo with valid dimensions that violates Telegram's sum or ratio constraints, compute a target canvas that:

1. never upscales the source;
2. preserves the source aspect ratio;
3. pads the shorter axis when necessary instead of cropping content to satisfy the 20:1 ratio; and
4. scales the final canvas so its width-plus-height is at most 10,000.

The normalizer will produce a single-frame JPEG with a neutral background for padding. The output must be non-empty, recognizable as an image, no larger than `qq_media_download_max_bytes`, and pass the same dimension inspector before replacing the original bytes.

A 2048×13301 JPEG is therefore downscaled into a compliant portrait image rather than turning the entire three-item album into a failure.

Cropping was rejected because it silently discards message content. Sending an overlong image as a document was rejected because it changes album semantics and cannot occupy a Telegram photo media-group position.

### 3. Use the existing media executable through a bounded, non-shell process

Run the configured `ffmpeg_path` on the bridge blocking executor using an argument-vector process launcher rather than shell interpolation. Each normalization processes one frame, uses one codec thread, has a 15-second deadline, and captures only a bounded diagnostic tail for internal classification. Timeout or non-zero exit produces `normalization_failed`; decoder text is not forwarded to users.

At most one normalization runs at a time within one media batch. Inputs declaring more than 64 megapixels receive `unsafe_dimensions` without invoking the decoder. This allows the 27.2-megapixel regression image while bounding ordinary decode memory and CPU exposure. The parent operation awaits all admitted normalization work before releasing actor-generation state.

Adding an in-process image library was rejected for this change because the bridge already deploys and configures ffmpeg, while a new decoder would expand the actor package and its vulnerability/update surface.

### 4. Model inspection and normalization as per-item outcomes

Introduce a testable photo-normalization component with an injectable process runner. For each `DownloadedImage`, it returns one of:

- unchanged and compliant;
- normalized and compliant;
- invalid/unsafe/normalization failure.

Failures flow into the existing placeholder substitution path at the same original index. Extend result accounting with a normalization count separate from the replacement-index set: normalization does not count as replacement, while a failed normalization that uses a placeholder does. URL-sanitization and download replacement de-duplication remain unchanged.

The component runs after ordered downloads and before final `TemporaryMediaUploads` materialization, so the multipart builder receives only compliant bytes and existing cleanup ownership remains authoritative.

### 5. Keep diagnostics stable and non-sensitive

Logs and user-visible metadata may expose item index, original dimensions, normalized dimensions, `normalized_count`, and stable categories `invalid_dimensions`, `unsafe_dimensions`, and `normalization_failed`. They must not expose source URLs, bot credentials, ffmpeg command lines containing source data, complete stderr, or complete Telegram responses.

The existing aggregate `multipart_upload/invalid_dimensions` category remains as a final defense if Telegram rejects a request despite preflight, but expected dimension violations should be resolved per item before upload.

## Risks / Trade-offs

- **[ffmpeg is unavailable or rejects a valid source]** → Treat that item as `normalization_failed`, substitute the existing placeholder, and preserve peers.
- **[Encoded headers claim hostile dimensions]** → Use checked parsing, reject dimensions above 64 megapixels before decode, serialize normalization per batch, and enforce a process deadline.
- **[JPEG conversion loses alpha or animation]** → Composite on a neutral background and document that photo fallback preserves visible content, not animation or transparency semantics.
- **[Rounding produces a boundary violation]** → Calculate targets with checked integer arithmetic below or at the Telegram limits and re-inspect the output before upload.
- **[Normalization output exceeds 10 MiB]** → Reject it as `normalization_failed` and use the placeholder; never raise the existing response/body ceiling.
- **[Telegram applies undocumented validation]** → Retain stable final-upload diagnostics and the previous atomic-failure path for errors not prevented by local constraints.

## Migration Plan

1. Add the inspector/normalizer and unit fixtures, including the 2048×13301 regression case.
2. Integrate it into the QQ multipart fallback before temporary upload materialization.
3. Deploy the rebuilt bridge actor and reload the actor runtime; no configuration or database migration is required when `libobcx_core.so` is unchanged.
4. Verify logs report one normalization and zero replacements for the regression album.
5. Roll back by restoring the prior bridge actor artifact and issuing another actor reload.

## Open Questions

None. The limits in this design follow Telegram's documented photo constraints and use fixed defensive execution bounds rather than adding new operator configuration.
