## ADDED Requirements

### Requirement: Multipart fallback enforces Telegram photo dimensions

Before constructing a multipart Telegram media group, the bridge SHALL inspect every downloaded photo's encoded dimensions. A directly accepted photo MUST have positive dimensions, a width-plus-height no greater than 10,000 pixels, and a larger-to-smaller dimension ratio no greater than 20. The bridge SHALL leave compliant photo bytes unchanged and SHALL attempt a bounded normalization for an otherwise valid photo that violates either Telegram dimension constraint.

Normalization MUST NOT upscale the source, MUST preserve its aspect ratio and visible content, MUST use padding rather than cropping when the source ratio exceeds 20, and MUST produce an output that satisfies the same dimension checks and the configured `qq_media_download_max_bytes` ceiling. If inspection or normalization fails, the item SHALL enter the existing per-item placeholder path without discarding valid peers.

#### Scenario: Overlong JPEG is normalized without losing its peers

- **WHEN** a multipart fallback batch contains a 2048×13301 JPEG and two compliant photos
- **THEN** the bridge uploads a dimension-compliant, aspect-preserving representation at the first position and retains the other two photos in their original positions

#### Scenario: Extreme aspect ratio is padded instead of cropped

- **WHEN** an otherwise valid photo has a larger-to-smaller dimension ratio greater than 20
- **THEN** normalization preserves the complete image, pads the shorter axis, and produces a ratio no greater than 20 without upscaling

#### Scenario: Photo already satisfies Telegram constraints

- **WHEN** a downloaded photo has positive dimensions, a width-plus-height no greater than 10,000, and a dimension ratio no greater than 20
- **THEN** the bridge sends the original encoded bytes without normalization

#### Scenario: Encoded dimensions cannot be inspected

- **WHEN** an image signature is recognized but its encoded dimensions are zero, truncated, malformed, or unsupported by the inspector
- **THEN** that item is classified as `invalid_dimensions` and replaced at its original position while compliant peers continue

#### Scenario: Normalized output is not acceptable

- **WHEN** transformation fails or its output is empty, unrecognized, over the configured byte ceiling, or still violates Telegram's dimension constraints
- **THEN** that item is classified as `normalization_failed` and replaced at its original position while compliant peers continue

### Requirement: Photo-dimension recovery is bounded and observable

The bridge SHALL reject a photo declaring more than 64 megapixels as `unsafe_dimensions` before invoking a full decoder. It MUST execute normalization on the configured blocking executor, MUST admit no more than one active normalization per media batch, MUST impose a finite per-item execution deadline, and MUST await admitted work before releasing operation or actor-generation state.

Replacement accounting SHALL count only placeholder substitutions; successful dimension normalization SHALL be reported separately. Logs and user-visible diagnostics MAY contain item indices, dimensions, normalization counts, replacement counts, and stable categories, but MUST NOT contain signed QQ URLs, bot credentials, full decoder output, command lines containing source data, or complete Telegram responses.

#### Scenario: Declared dimensions exceed the decode budget

- **WHEN** an encoded photo declares more than 64 megapixels
- **THEN** the bridge does not invoke the decoder, classifies the item as `unsafe_dimensions`, and substitutes the placeholder at that position

#### Scenario: Several photos require normalization

- **WHEN** multiple photos in one fallback batch violate recoverable Telegram dimension constraints
- **THEN** the bridge normalizes them with at most one active transformation for that batch and preserves input order

#### Scenario: Normalization exceeds its deadline

- **WHEN** the image transformation does not complete within the finite per-item deadline
- **THEN** the bridge terminates or abandons the transformation safely, classifies the item as `normalization_failed`, and continues placeholder recovery

#### Scenario: Successful normalization is not a replacement

- **WHEN** one photo is normalized successfully and a different photo is replaced by a placeholder
- **THEN** aggregate reporting records one normalization and one replacement without counting the normalized index as replaced

#### Scenario: Dimension diagnostics remain non-sensitive

- **WHEN** inspection or normalization fails for a signed QQ media URL
- **THEN** logs and forwarded diagnostics expose a stable dimension category and item index without exposing the URL or full tool/API response
