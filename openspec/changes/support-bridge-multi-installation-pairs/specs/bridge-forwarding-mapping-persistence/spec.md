## MODIFIED Requirements

### Requirement: Direct forwarding propagates its observed target identity
The bridge SHALL propagate the exact source installation and the target installation/message id parsed from a successful bot response through the platform handler and forwarding-runtime result. The runtime MUST NOT query persistent mappings after a direct handler completes solely to reconstruct that result, and it MUST NOT infer either installation from platform after route selection.

#### Scenario: A new QQ message is sent to Telegram
- **WHEN** one configured OneBot installation routes a QQ-origin message to its paired Telegram installation and Telegram returns a valid target message id
- **THEN** the QQ handler returns both exact installations and that target id through the forwarding result without persisting and rereading the primary mapping

#### Scenario: A new Telegram message is sent to QQ
- **WHEN** one configured Telegram installation routes a message to its paired OneBot installation and QQ returns a valid target message id
- **THEN** the Telegram handler returns both exact installations and that target id through the forwarding result without persisting and rereading the primary mapping

#### Scenario: An inline QQ media group is sent to Telegram
- **WHEN** the immediately awaited Telegram media-group response contains a valid primary target message id
- **THEN** the QQ forwarding path returns that primary id with the selected source and target installations and leaves its one primary mapping write to the actor boundary

#### Scenario: A handler does not produce a direct delivery
- **WHEN** a message is skipped, deferred to media-group processing, fails to send, or is durably enqueued for retry
- **THEN** the handler returns an explicit non-forwarded outcome and the runtime does not infer success or installations with a mapping lookup

### Requirement: One owner persists a new direct mapping
For a newly delivered direct message, `BridgeActor` SHALL be the only layer that persists the primary source-to-target mapping. The mapping SHALL include exact source and target installations in addition to the existing platform and native message fields. The platform handler and `BridgeForwardingRuntime` MUST NOT persist the same primary mapping, and the actor SHALL publish `MessageForwarded` only after its one scoped write succeeds.

#### Scenario: Direct mapping persistence succeeds
- **WHEN** a forwarding result contains complete source and target installation/platform/message identities and the repository accepts it
- **THEN** the bridge performs exactly one primary mapping write and publishes `MessageForwarded` with the same source and target bot ids

#### Scenario: Direct mapping persistence fails
- **WHEN** the target bot has returned a valid id but the actor's scoped mapping write throws or reports failure
- **THEN** the bridge publishes no `MessageForwarded`, reports a mapping-persistence failure, and does not automatically repeat the bot send

#### Scenario: A forwarding result is incomplete
- **WHEN** a forwarding result marked as delivered lacks a source installation, source platform, source message id, target installation, target platform, or target message id
- **THEN** the actor performs no mapping write and reports `missing_forward_mapping`

### Requirement: De-duplication reuses the existing mapping result
The direct forwarding handler SHALL perform any required pre-send de-duplication lookup using the exact source installation, existing source identity, and selected target installation before invoking the target bot. When that lookup finds a mapping, the handler SHALL return its exact target installation and message id as already persisted. The runtime and actor MUST NOT issue another recovery read or mapping write for that delivery.

#### Scenario: Equal ids exist in separate pairs
- **WHEN** a source message id is already mapped for one installation pair but an equal id arrives from another pair
- **THEN** the second pair does not reuse the first mapping and independently follows its own pre-send result

#### Scenario: A scoped source message is already mapped
- **WHEN** the exact source-installation/source-message/target-installation lookup finds a mapping
- **THEN** the target bot is not called and the existing target id reaches completion with zero additional primary mapping writes

#### Scenario: A scoped source message is not mapped
- **WHEN** the exact lookup misses and the selected target bot later returns a valid id
- **THEN** the outcome is marked as a new delivery and the actor performs the single required scoped mapping write

### Requirement: Specialized persistence paths remain single-owned
Retry completion and deferred media-group forwarding SHALL retain their specialized mapping ownership when their consistency boundary includes retry state cleanup, multiple source mappings, or media-group state. Those paths SHALL persist the exact source and target installations and MUST NOT add a duplicate `BridgeActor` primary mapping write.

#### Scenario: A retry succeeds
- **WHEN** the exact target-installation retry callback receives a valid target id and atomically completes its durable retry state
- **THEN** its installation-scoped mapping and retry-row cleanup remain owned by the retry path and no direct-forward actor write is added

#### Scenario: A Telegram media group is flushed
- **WHEN** a deferred album from one Telegram installation maps multiple source ids to one QQ target id
- **THEN** the media-group path persists its installation-scoped mapping set and state without fabricating a scalar direct result for an additional actor write

### Requirement: Mapping round-trip regression is measured
The bridge SHALL have automated coverage that distinguishes repository operation counts from final row counts for direct forwarding. Counts SHALL remain valid when two installation pairs use colliding native ids. The business simulation SHALL finish and verify all required persistence before reporting a candidate throughput result.

#### Scenario: A successful direct route is tested
- **WHEN** a focused test executes one uncached successful direct forwarding operation for one pair
- **THEN** it observes one scoped primary mapping write, no post-send mapping-recovery read, and one durable mapping row

#### Scenario: Two pairs use equal message ids
- **WHEN** focused forwarding tests persist equal platform/message ids for two disjoint installation pairs
- **THEN** two rows remain queryable in isolation without increasing per-delivery write or post-send-read counts

#### Scenario: The business benchmark completes
- **WHEN** the isolated fake-operation-client business simulation reports throughput for multiple pairs
- **THEN** all submitted messages have reached terminal completion, required scoped rows are present, and measured mapping-operation counts satisfy the single-write contract
