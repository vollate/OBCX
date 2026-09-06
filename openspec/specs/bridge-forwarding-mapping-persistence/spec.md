# bridge-forwarding-mapping-persistence Specification

## Purpose
Define direct Bridge forwarding result propagation and single-owner mapping persistence without post-send recovery reads.

## Requirements
### Requirement: Direct forwarding propagates its observed target identity
The bridge SHALL propagate the complete source installation/platform/conversation/message identity and the target installation/platform/conversation plus message id parsed from a successful bot response through the platform handler and forwarding-runtime result. The runtime MUST NOT query persistent mappings after a direct handler completes solely to reconstruct that result, and it MUST NOT infer either installation or conversation after route selection.

#### Scenario: A new QQ message is sent to Telegram
- **WHEN** one configured OneBot installation routes a QQ-origin message from one group conversation to its paired Telegram installation/chat and Telegram returns a valid target message id
- **THEN** the QQ handler returns both complete conversation-scoped identities through the forwarding result without persisting and rereading the primary mapping

#### Scenario: A new Telegram message is sent to QQ
- **WHEN** one configured Telegram installation routes a message from one chat conversation to its paired OneBot installation/group and QQ returns a valid target message id
- **THEN** the Telegram handler returns both complete conversation-scoped identities through the forwarding result without persisting and rereading the primary mapping

#### Scenario: An inline QQ media group is sent to Telegram
- **WHEN** the immediately awaited Telegram media-group response contains a valid primary target message id
- **THEN** the QQ forwarding path returns that primary id with selected source and target installations/conversations and leaves its one primary mapping write to the actor boundary

#### Scenario: A handler does not produce a direct delivery
- **WHEN** a message is skipped, deferred to media-group processing, fails to send, is blocked by ambiguous mapping state, or is durably enqueued for retry
- **THEN** the handler returns an explicit non-forwarded or failed outcome and the runtime does not infer success or identity with a mapping lookup

### Requirement: One owner persists a new direct mapping
For a newly delivered direct message, `BridgeActor` SHALL be the only layer that persists the primary source-to-target mapping. The mapping SHALL contain complete exact source and target message identities. The platform handler and `BridgeForwardingRuntime` MUST NOT persist the same primary mapping, and the actor SHALL publish `MessageForwarded` only after its one conversation-scoped write succeeds.

#### Scenario: Direct mapping persistence succeeds
- **WHEN** a forwarding result contains complete source and target installation/platform/conversation/message identities and the repository accepts it
- **THEN** Bridge performs exactly one primary mapping write and publishes `MessageForwarded` with the same source and target bot, conversation, and message ids

#### Scenario: Direct mapping persistence fails
- **WHEN** the target bot has returned a valid id but the actor's exact mapping write throws or reports failure
- **THEN** Bridge publishes no `MessageForwarded`, reports a mapping-persistence failure, and does not automatically repeat the bot send

#### Scenario: A forwarding result is incomplete
- **WHEN** a forwarding result marked as delivered lacks a source installation, source platform, source conversation, source message id, target installation, target platform, target conversation, or target message id
- **THEN** the actor performs no mapping write and reports `missing_forward_mapping`

#### Scenario: Equal ids exist in another conversation
- **WHEN** an exact upsert encounters the same native source or target id in another group/chat on the same installations
- **THEN** it neither replaces nor modifies the other conversation's row

### Requirement: De-duplication reuses the existing mapping result
The direct forwarding handler SHALL perform any required pre-send de-duplication lookup using the complete source identity and selected exact target scope before invoking the target bot. When that lookup finds one mapping, the handler SHALL return its complete target identity as already persisted. The runtime and actor MUST NOT issue another recovery read or mapping write for that delivery. Ambiguous or corrupt lookup state MUST fail before provider dispatch.

#### Scenario: Equal ids exist in separate conversations or pairs
- **WHEN** a native source message id is mapped in another conversation or installation pair but not in the current complete source scope
- **THEN** the current source does not reuse that mapping and independently follows only its own exact route

#### Scenario: A complete source identity is already mapped
- **WHEN** the exact source-installation/source-conversation/source-message and target-installation/target-conversation lookup finds one mapping
- **THEN** the target bot is not called and the existing complete target identity reaches completion with zero additional primary mapping writes

#### Scenario: A complete source identity is not mapped
- **WHEN** the exact lookup has no current or legacy candidate and the selected target bot later returns a valid id
- **THEN** the outcome is marked as a new delivery and the actor performs the single required exact mapping write

#### Scenario: Legacy mapping candidates are ambiguous
- **WHEN** pre-send resolution sees incomplete legacy candidates that cannot be proven to belong to the exact source and target conversations
- **THEN** Bridge reports `ambiguous_message_mapping` with no provider call, recovery read, or mapping write

### Requirement: Specialized persistence paths remain single-owned
Retry completion and deferred media-group forwarding SHALL retain their specialized mapping ownership when their consistency boundary includes retry state cleanup, several source mappings, or media-group state. Those paths SHALL persist complete source and target identities and MUST NOT add a duplicate `BridgeActor` primary mapping write. A fan-in media group SHALL identify exactly one semantic primary source for reverse lookup.

#### Scenario: A retry succeeds
- **WHEN** an exact target-installation/conversation retry callback receives a valid target id and atomically completes its durable retry state
- **THEN** its complete conversation-scoped mapping and retry-row cleanup remain owned by the retry path and no direct-forward actor write is added

#### Scenario: A Telegram media group is flushed
- **WHEN** a deferred album from one Telegram installation/chat maps several source ids to one QQ installation/group target id
- **THEN** the media-group path persists its complete mapping set, exactly one primary source, and media-group state without fabricating a scalar direct result for another actor write

#### Scenario: Equal albums exist in another chat
- **WHEN** another Telegram chat uses the same native media-group and message ids
- **THEN** its specialized writes remain independent and do not update the first chat's mappings or primary role

### Requirement: Mapping round-trip regression is measured
The bridge SHALL have automated coverage that distinguishes repository operation counts from final row counts for direct forwarding. Counts SHALL remain valid when conversations and installation pairs use colliding native ids. The business simulation SHALL finish and verify all required persistence before reporting a candidate throughput result.

#### Scenario: A successful direct route is tested
- **WHEN** a focused test executes one uncached successful direct forwarding operation for one complete source/target route
- **THEN** it observes one exact primary mapping write, no post-send mapping-recovery read, and one durable mapping row

#### Scenario: The business benchmark completes
- **WHEN** the isolated fake-operation-client business simulation reports throughput for several conversations and pairs
- **THEN** all submitted messages have reached terminal completion, required exact rows are present, and measured mapping-operation counts satisfy the single-write contract

#### Scenario: Two pairs use equal message ids
- **WHEN** focused forwarding tests persist equal platform/message ids for two disjoint installation pairs
- **THEN** two rows remain queryable in isolation without increasing per-delivery write or post-send-read counts

#### Scenario: Two conversations use equal message ids
- **WHEN** focused forwarding tests persist equal platform/message ids in two group/chat conversations on the same installations
- **THEN** both rows remain queryable in isolation without increasing per-delivery write or post-send-read counts

#### Scenario: An already persisted route is tested
- **WHEN** the same complete source identity is processed again
- **THEN** it observes one exact pre-send read, no provider call, no post-send read, and no additional mapping write
