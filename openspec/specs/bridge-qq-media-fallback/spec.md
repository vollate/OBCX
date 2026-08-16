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
