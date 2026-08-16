## ADDED Requirements

### Requirement: Direct forwarding propagates its observed target identity
The bridge SHALL propagate the target message id parsed from a successful bot
response through the platform handler and forwarding-runtime result. The
runtime MUST NOT query persistent mappings after a direct handler completes
solely to reconstruct that result.

#### Scenario: A new QQ message is sent to Telegram
- **WHEN** Telegram accepts a direct QQ-origin message and returns a valid target message id
- **THEN** the QQ handler returns that target id through the forwarding result without first persisting and rereading the primary mapping

#### Scenario: A new Telegram message is sent to QQ
- **WHEN** QQ accepts a direct Telegram-origin message and returns a valid target message id
- **THEN** the Telegram handler returns that target id through the forwarding result without first persisting and rereading the primary mapping

#### Scenario: An inline QQ media group is sent to Telegram
- **WHEN** the immediately awaited Telegram media-group response contains a valid primary target message id
- **THEN** the QQ forwarding path returns that primary id and leaves its one primary mapping write to the actor boundary

#### Scenario: A handler does not produce a direct delivery
- **WHEN** a message is skipped, deferred to media-group processing, fails to send, or is durably enqueued for retry
- **THEN** the handler returns an explicit non-forwarded outcome and the runtime does not infer success with a mapping lookup

### Requirement: One owner persists a new direct mapping
For a newly delivered direct message, `BridgeActor` SHALL be the only layer
that persists the primary source-to-target mapping. The platform handler and
`BridgeForwardingRuntime` MUST NOT persist the same primary mapping. The actor
SHALL publish `MessageForwarded` only after its one mapping write succeeds.

#### Scenario: Direct mapping persistence succeeds
- **WHEN** a forwarding result contains a complete new source-to-target mapping and the repository accepts it
- **THEN** the bridge performs exactly one primary mapping write and publishes `MessageForwarded` with the same source and target ids

#### Scenario: Direct mapping persistence fails
- **WHEN** the target bot has returned a valid id but the actor's mapping write throws or reports failure
- **THEN** the bridge publishes no `MessageForwarded`, reports a mapping-persistence failure, and does not automatically repeat the bot send

#### Scenario: A forwarding result is incomplete
- **WHEN** a forwarding result marked as delivered lacks a source platform, source message id, target platform, or target message id
- **THEN** the actor performs no mapping write and reports `missing_forward_mapping`

### Requirement: De-duplication reuses the existing mapping result
The direct forwarding handler SHALL perform any required pre-send
de-duplication lookup before invoking the target bot. When that lookup finds a
mapping, the handler SHALL return its target id as already persisted. The
runtime and actor MUST NOT issue another recovery read or mapping write for
that delivery.

#### Scenario: A source message is already mapped
- **WHEN** the pre-send lookup finds the source-platform, source-message, and target-platform mapping
- **THEN** the target bot is not called and the existing target id reaches completion with zero additional primary mapping writes

#### Scenario: A source message is not mapped
- **WHEN** the pre-send lookup misses and the target bot later returns a valid id
- **THEN** the outcome is marked as a new delivery and the actor performs the single required mapping write

### Requirement: Specialized persistence paths remain single-owned
Retry completion and deferred media-group forwarding SHALL retain their
specialized mapping ownership when their consistency boundary includes retry
state cleanup, multiple source mappings, or media-group state. Processing
those outcomes MUST NOT add a duplicate `BridgeActor` primary mapping write.

#### Scenario: A retry succeeds
- **WHEN** the retry worker receives a valid target id and atomically completes its durable retry state
- **THEN** its mapping and retry-row cleanup remain owned by the retry path and no direct-forward actor write is added

#### Scenario: A Telegram media group is flushed
- **WHEN** the deferred media-group callback maps multiple Telegram source ids to one QQ target id
- **THEN** the media-group path persists its mapping set and media-group state without fabricating a scalar direct result for an additional actor write

### Requirement: Mapping round-trip regression is measured
The bridge SHALL have automated coverage that distinguishes repository
operation counts from final row counts for direct forwarding. The business
simulation SHALL finish and verify all required persistence before reporting a
candidate throughput result.

#### Scenario: A successful direct route is tested
- **WHEN** a focused test executes one uncached successful direct forwarding operation
- **THEN** it observes one primary mapping write, no post-send mapping-recovery read, and one durable mapping row

#### Scenario: The business benchmark completes
- **WHEN** the isolated mock-bot business simulation reports throughput for the optimized bridge
- **THEN** all submitted messages have reached terminal completion, required database rows are present, and the measured mapping-operation counts satisfy the single-write contract
