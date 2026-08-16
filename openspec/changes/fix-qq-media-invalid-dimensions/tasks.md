## 1. Dimension inspection and geometry policy

- [x] 1.1 Add a QQ fallback photo-inspection result type, stable dimension failure categories, and constants for Telegram's 10,000-pixel dimension sum, 20:1 ratio, and 64-megapixel decode ceiling.
- [x] 1.2 Implement checked encoded-dimension parsing for JPEG, PNG, GIF, WebP, and BMP without fully decoding image bodies.
- [x] 1.3 Implement deterministic target image/canvas geometry calculation that never upscales, preserves aspect ratio, pads extreme ratios, and remains within Telegram limits.
- [x] 1.4 Add unit tests for compliant boundaries, zero/truncated/overflow headers, supported formats, unsafe pixel counts, extreme ratios, and the 2048×13301 regression geometry.

## 2. Bounded photo normalization

- [x] 2.1 Add an injectable photo normalizer that accepts downloaded bytes and returns unchanged, normalized, or per-item failure outcomes.
- [x] 2.2 Launch the configured ffmpeg executable with an argument vector rather than a shell command, limiting conversion to one frame and one codec thread.
- [x] 2.3 Enforce a 15-second per-item deadline, bounded diagnostic capture, one active normalization per batch, and complete child-process cleanup on success, failure, timeout, and cancellation.
- [x] 2.4 Validate normalized output MIME, byte size, dimensions, and pixel budget before accepting it, and remove every input/output temporary file on all paths.
- [x] 2.5 Add normalizer tests using a controllable process runner for success, unavailable ffmpeg, malformed output, oversized output, timeout, cancellation, and temporary-file cleanup.

## 3. Multipart fallback integration

- [x] 3.1 Run ordered dimension inspection and required normalization after bounded full-media downloads and before final multipart materialization on the configured blocking executor.
- [x] 3.2 Route `invalid_dimensions`, `unsafe_dimensions`, and `normalization_failed` outcomes through the existing placeholder substitution path without changing valid peer bytes or positions.
- [x] 3.3 Extend fallback results and captions/logs with a normalization count separate from de-duplicated replacement accounting.
- [x] 3.4 Preserve final `multipart_upload/invalid_dimensions` handling as a non-sensitive defense for Telegram rejections not prevented by local preflight.

## 4. Regression and behavior verification

- [x] 4.1 Add formatter tests proving a 2048×13301 first item is normalized while 1024×372 and 960×960 peers remain in a successful three-photo group.
- [x] 4.2 Add tests proving unsafe, uninspectable, and failed-normalization items are replaced individually and do not atomically discard compliant peers.
- [x] 4.3 Add tests proving compliant inputs are byte-identical, several normalization candidates are processed sequentially, and normalization/replacement counts remain distinct.
- [x] 4.4 Verify ordinary QQ albums and merged-forward image batches share the same dimension-recovery behavior and retain Telegram's 2-to-10 and 9-plus-2 batching rules.

## 5. Documentation and validation

- [x] 5.1 Document Telegram photo-dimension normalization, fixed resource bounds, ffmpeg fallback behavior, and stable diagnostic categories in the bridge README.
- [x] 5.2 Run bridge unit tests, root Telegram/media tests, standalone installed-SDK bridge tests, `nix fmt`, `git diff --check`, and strict OpenSpec validation.
