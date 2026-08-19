# bridge-qq-media-fallback Specification

## Purpose
Define bounded QQ media downloads and per-item multipart fallback so oversized or invalid images cannot discard valid peers or expose sensitive diagnostics.

## Requirements
### Requirement: QQ fallback media downloads use a validated size policy
The bridge SHALL load an immutable `qq_media_download_max_bytes` actor-generation setting for full QQ media downloads. The setting SHALL default to 10 MiB, MUST be positive, and MUST NOT exceed 10 MiB. Invalid values SHALL reject startup or candidate reload before activation.

#### Scenario: Default media policy is used
- **WHEN** bridge configuration omits `qq_media_download_max_bytes`
- **THEN** full QQ media downloads use a 10 MiB response-body limit

#### Scenario: Operator configures a lower valid limit
- **WHEN** bridge configuration supplies a positive value no larger than 10 MiB
- **THEN** the active generation uses exactly that limit for full QQ media downloads

#### Scenario: Candidate media policy is invalid
- **WHEN** a candidate generation configures zero, a negative value, or a value larger than 10 MiB
- **THEN** validation rejects the candidate without replacing the active generation

### Requirement: Full-media downloads are bounded and image-specific
The bridge SHALL apply the configured body limit to each full-media response and SHALL request identity transfer encoding. A successful fallback item MUST contain a non-empty body identifiable as image content. Reachability probes MUST remain separate from full-body acceptance.

#### Scenario: QQ photo is between the former and configured limits
- **WHEN** an otherwise valid QQ image is larger than 8 MiB but no larger than the configured 10 MiB limit
- **THEN** the bridge downloads it successfully for multipart upload

#### Scenario: QQ photo exceeds the configured limit
- **WHEN** a QQ image declares or streams more bytes than `qq_media_download_max_bytes`
- **THEN** that item receives an over-limit outcome without buffering the complete image

#### Scenario: Reachability probe succeeds for an oversized image
- **WHEN** a range probe confirms that a QQ URL is reachable but the later full body exceeds the configured limit
- **THEN** the full download is treated as an item failure rather than as proof that the complete image is acceptable

#### Scenario: Successful response is not an image
- **WHEN** a media URL returns an empty body or content that cannot be identified as an image
- **THEN** that item receives an invalid-media outcome

### Requirement: Download outcomes preserve item identity and operation lifetime
The bridge SHALL produce exactly one success or failure outcome for every input media item, in input order. It MUST limit active full-media downloads to three and MUST join all download workers before the parent bridge operation completes or releases actor-generation state.

#### Scenario: Downloads complete out of order
- **WHEN** later media downloads finish before earlier downloads
- **THEN** the outcome vector remains aligned with original media indices

#### Scenario: Several items are pending
- **WHEN** a batch contains more than three media items
- **THEN** no more than three full-media downloads are active simultaneously

#### Scenario: Parent operation is cancelled
- **WHEN** cancellation interrupts a fallback download batch
- **THEN** worker completion is accounted for and no detached worker later accesses released invocation or generation state

### Requirement: One failed item does not discard its valid peers
The multipart fallback SHALL retain every successfully downloaded item and SHALL substitute a placeholder at each failed item's original position. It MUST attempt the configured placeholder at most once per batch and MUST use an embedded image placeholder if that remote placeholder is unavailable or invalid.

#### Scenario: One item in a batch exceeds the limit
- **WHEN** one item is oversized and the remaining items download successfully
- **THEN** the oversized item is replaced and the valid items continue to multipart upload in their original positions

#### Scenario: Several items fail for different reasons
- **WHEN** a batch contains expired, oversized, and invalid-image URLs alongside valid images
- **THEN** each failed index is replaced without converting a valid peer into a batch failure

#### Scenario: Configured placeholder cannot be downloaded
- **WHEN** at least one item needs replacement and the configured placeholder download fails
- **THEN** the bridge uses its embedded image placeholder and continues the multipart upload

#### Scenario: Multiple items require the configured placeholder
- **WHEN** several item downloads fail in the same batch
- **THEN** the bridge downloads the configured placeholder no more than once and reuses its validated bytes

### Requirement: Multipart fallback preserves batch semantics and replacement accounting
The bridge SHALL preserve media order and Telegram's 2-to-10 item media-group rules after substitution. Replacement reporting SHALL count the union of indices replaced during URL sanitization and full-media fallback, and MUST NOT count one index twice.

#### Scenario: Sanitization and full download replace different items
- **WHEN** URL sanitization replaces one index and multipart downloading replaces another index
- **THEN** the upload keeps both positions and reports two replaced images

#### Scenario: Previously replaced placeholder needs embedded fallback
- **WHEN** URL sanitization already marked an index replaced and its configured placeholder later fails to download
- **THEN** embedded fallback preserves the item while the replacement total counts that index once

#### Scenario: Eleven images are forwarded
- **WHEN** eleven QQ images require Telegram media-group delivery
- **THEN** the bridge continues to split them into groups of nine and two rather than ten and one

### Requirement: Media failures expose stable and non-sensitive diagnostics
The bridge SHALL classify media transport, over-limit, empty-body, and invalid-image failures before producing logs or user-visible text. User-visible diagnostics MUST NOT include complete signed QQ URLs, bot credentials, Boost source paths, or complete remote response bodies.

#### Scenario: Beast rejects an oversized response
- **WHEN** a low-level parser reports `body limit exceeded` with library source details
- **THEN** the bridge reports the item index, stable over-limit category, and configured limit without forwarding the low-level source path to the user

#### Scenario: Placeholder recovery succeeds
- **WHEN** a failed media item is replaced and the multipart group is accepted
- **THEN** logs and caption metadata expose the replacement count and successful fallback mode without exposing the original signed URL

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
