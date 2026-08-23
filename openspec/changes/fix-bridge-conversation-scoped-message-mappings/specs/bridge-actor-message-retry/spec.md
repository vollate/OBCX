## MODIFIED Requirements

### Requirement: Enabled bridge message retry is operational in actor mode
When `actors.bridge.config.enable_retry_queue` is true, the bridge actor SHALL create one message retry manager for its runtime generation, register every configured exact target installation, and provide that manager to the QQ and Telegram forwarding handlers before processing a send. The one generation-owned manager SHALL multiplex pairs and conversations without creating a worker per pair or group. An enabled generation MUST NOT report a failed send as retry-disabled.

#### Scenario: QQ-to-Telegram initial send fails
- **WHEN** forwarding from one exact OneBot installation/group conversation to its paired Telegram installation/chat fails definitely before submission and retry is enabled
- **THEN** Bridge persists and schedules one retry containing both complete conversation-scoped identities, the outgoing message, and target topic metadata

#### Scenario: Telegram-to-QQ initial send fails
- **WHEN** forwarding from one exact Telegram installation/chat conversation to its paired OneBot installation/group fails definitely before submission and retry is enabled
- **THEN** Bridge persists and schedules one retry containing both complete conversation-scoped identities and the outgoing message

#### Scenario: Equal ids fail in two conversations
- **WHEN** retryable sends use equal native source ids in different chats or groups on the same installations
- **THEN** the one generation worker retains independent conversation-scoped entries and neither replaces or dispatches the other

#### Scenario: Retry is explicitly disabled
- **WHEN** the same send failure occurs in a generation whose Bridge configuration explicitly disables retry
- **THEN** Bridge creates no retry worker or retry row and may report that retry is disabled

#### Scenario: Enabled retry cannot initialize
- **WHEN** retry is enabled but its worker, repository, exact-installation callbacks, exact-conversation schema, or migration cannot initialize
- **THEN** Bridge forwarding fails with a retry-unavailable diagnostic and does not report that retry was disabled

### Requirement: Retry callbacks resend through process-owned bots
The bridge retry worker SHALL submit QQ group sends, Telegram group sends, and Telegram topic sends through `BotOperationClient` using the exact target installation and target conversation persisted in each retry. It MUST NOT select a callback or conversation by platform, current route, current pair order, pair alias, or native message id alone; resolve `BotRegistry`; retain `IBot` or a provider interface; use a concrete/dynamic cast; invoke a connection manager; or call a provider message-lookup action. It SHALL accept a retry as successful only when the typed result contains a valid target message reference for the persisted installation/conversation and SHALL distinguish definitely retryable failure, terminal failure, and possibly submitted outcome.

#### Scenario: Telegram group retry succeeds
- **WHEN** a QQ-origin retry names an enabled Telegram target installation/chat without a positive topic id and typed dispatch returns a matching target message reference
- **THEN** the worker records that complete target identity and completes only that conversation-scoped retry

#### Scenario: Telegram topic retry succeeds
- **WHEN** a QQ-origin retry names a Telegram chat plus positive topic id and typed dispatch returns a matching target message reference
- **THEN** the worker uses `telegram.message.send_topic`, records the containing chat identity and native message id, and completes only that retry

#### Scenario: QQ group retry succeeds
- **WHEN** a Telegram-origin retry names an enabled `onebot11.qq` target installation/group and typed dispatch returns a matching target message reference
- **THEN** the worker records that complete target identity and completes only that retry

#### Scenario: Persisted target installation or conversation is unavailable
- **WHEN** a restored retry names an installation or conversation that is no longer an exact configured dispatch route
- **THEN** the worker reports the typed unavailable route and never sends through another installation, group, or chat

#### Scenario: Target failure is definitely retryable
- **WHEN** exact dispatch reports a retryable failure proven to occur before provider submission
- **THEN** the worker records an unsuccessful attempt and retains the same conversation-scoped retry while attempts remain

#### Scenario: Target outcome may have been submitted
- **WHEN** exact dispatch reports `PossiblySubmitted`
- **THEN** the worker stops automatic attempts for that entry according to the existing finite-attempt policy and reports an outcome-unknown diagnostic

### Requirement: Persistent retry state and message mappings remain consistent
The bridge SHALL persist retry identity by complete source installation/platform/conversation/message identity and exact target installation/platform/conversation. It SHALL preserve pending attempts across process restart and actor-runtime reload. After a successful resend, it SHALL persist a complete source-to-target message mapping and remove only the corresponding conversation-scoped retry row. Pending version-2 retries MUST migrate exactly to version 3 or block migration; they MUST NOT be silently archived, dropped, or retargeted.

#### Scenario: The same complete failure is enqueued again
- **WHEN** a failed source message with the same complete source and target scopes is added more than once
- **THEN** the durable queue contains one retry identity rather than parallel duplicate entries

#### Scenario: Equal native failures belong to different conversations or pairs
- **WHEN** equal platform/message ids are enqueued for different source conversations, target conversations, source installations, or target installations
- **THEN** the durable queue retains independent rows and completing one does not update or remove another

#### Scenario: A worker starts with pending rows
- **WHEN** the active Bridge generation starts its retry worker and version-3 pending rows exist
- **THEN** it restores exact installations, conversations, message content, target topic metadata, attempt counts, limits, and next-attempt times before processing them

#### Scenario: A retry succeeds
- **WHEN** a callback returns a target message reference matching the persisted target installation/conversation and repository operations succeed
- **THEN** the complete source-to-target mapping is queryable and only that exact retry row is no longer pending

#### Scenario: Pre-send mapping already exists
- **WHEN** the worker finds an existing mapping for the retry's complete source and target scopes
- **THEN** it performs no provider call and removes only the corresponding retry row according to existing cleanup safety

#### Scenario: A different conversation is already mapped
- **WHEN** an equal native source id has a mapping in another conversation
- **THEN** the worker does not treat that mapping as completion of the current retry

#### Scenario: Completion persistence fails
- **WHEN** complete mapping persistence or exact retry-row cleanup fails after a target response is received
- **THEN** Bridge reports the persistence failure and keeps local state safe for idempotent recovery rather than silently claiming complete cleanup

#### Scenario: Version-2 retry lacks a conversation
- **WHEN** schema-version-3 preflight cannot derive both conversations for a pending retry
- **THEN** migration fails before actor activation and no retry is sent, archived, or removed automatically
