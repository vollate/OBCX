# bridge-actor-message-retry Specification

## Purpose
TBD - created by archiving change restore-bridge-actor-message-retry. Update Purpose after archive.
## Requirements
### Requirement: Enabled bridge message retry is operational in actor mode
When `actors.bridge.config.enable_retry_queue` is true, the bridge actor SHALL create one message retry manager for its runtime generation, register every configured exact target installation, and provide that manager to the QQ and Telegram forwarding handlers before processing a send. The one generation-owned manager SHALL multiplex pairs and conversations without creating a worker per pair or group. An enabled generation MUST NOT report a failed send as retry-disabled.

#### Scenario: QQ-to-Telegram initial send fails
- **WHEN** forwarding from one exact OneBot installation/group conversation to its paired Telegram installation/chat fails definitely before submission and retry is enabled
- **THEN** Bridge persists and schedules one retry containing both complete conversation-scoped identities, the outgoing message, and target topic metadata

#### Scenario: Telegram-to-QQ initial send fails
- **WHEN** forwarding from one exact Telegram installation/chat conversation to its paired OneBot installation/group fails definitely before submission and retry is enabled
- **THEN** Bridge persists and schedules one retry containing both complete conversation-scoped identities and the outgoing message

#### Scenario: Retry is explicitly disabled
- **WHEN** the same send failure occurs in a generation whose Bridge configuration explicitly disables retry
- **THEN** Bridge creates no retry worker or retry row and may report that retry is disabled

#### Scenario: Enabled retry cannot initialize
- **WHEN** retry is enabled but its worker, repository, exact-installation callbacks, exact-conversation schema, or migration cannot initialize
- **THEN** Bridge forwarding fails with a retry-unavailable diagnostic and does not report that retry was disabled

#### Scenario: Two pairs fail concurrently
- **WHEN** retryable sends fail for two configured pairs
- **THEN** the one generation worker retains two installation-scoped entries and neither replaces or dispatches the other

#### Scenario: Equal ids fail in two conversations
- **WHEN** retryable sends use equal native source ids in different chats or groups on the same installations
- **THEN** the one generation worker retains independent conversation-scoped entries and neither replaces or dispatches the other

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

#### Scenario: Target failure is definitely retryable
- **WHEN** exact dispatch reports a retryable failure proven to occur before provider submission
- **THEN** the worker records an unsuccessful attempt and retains the same conversation-scoped retry while attempts remain

#### Scenario: Target outcome may have been submitted
- **WHEN** exact dispatch reports `PossiblySubmitted`
- **THEN** the worker stops automatic attempts for that entry according to the existing finite-attempt policy and reports an outcome-unknown diagnostic

#### Scenario: Persisted target installation or conversation is unavailable
- **WHEN** a restored retry names an installation or conversation that is no longer an exact configured dispatch route
- **THEN** the worker reports the typed unavailable route and never sends through another installation, group, or chat

### Requirement: Message retry policy is configuration-driven and bounded
The bridge actor SHALL derive message maximum attempts, base backoff, queue check interval, and maximum backoff from its immutable generation configuration. Attempt and interval values MUST be positive, base and check intervals MUST NOT exceed the maximum interval, and invalid settings MUST fail actor-aware configuration validation. Only typed failures marked retryable and `DefinitelyNotSubmitted` SHALL be rescheduled; terminal or `PossiblySubmitted` outcomes MUST NOT be automatically retried.

#### Scenario: Retry is rescheduled
- **WHEN** a resend attempt is definitely not submitted, retryable, and below its configured maximum
- **THEN** the worker increments the durable attempt count and schedules the next attempt using exponential backoff capped by the configured maximum interval

#### Scenario: Maximum attempts are exhausted
- **WHEN** a definitely failed resend reaches its configured maximum attempt count
- **THEN** the worker stops scheduling that entry, removes it according to the finite-attempt policy, and reports terminal exhaustion without logging the outgoing message

#### Scenario: Retry outcome is uncertain
- **WHEN** an attempt may have reached the provider
- **THEN** the worker does not schedule another attempt and records a secret-safe outcome-unknown diagnostic without introducing a new outbox or reconciliation table

#### Scenario: Retry configuration is invalid
- **WHEN** a bridge configuration contains a non-positive attempt or interval value or an interval greater than the configured maximum
- **THEN** startup or reload rejects that actor configuration before it becomes active

### Requirement: Persistent retry state and message mappings remain consistent
The bridge SHALL persist retry identity by complete source installation/platform/conversation/message identity and exact target installation/platform/conversation. It SHALL preserve pending attempts across process restart and actor-runtime reload. After a successful resend, it SHALL persist a complete source-to-target message mapping and remove only the corresponding conversation-scoped retry row. Pending version-2 retries MUST migrate exactly to version 3 or block migration; they MUST NOT be silently archived, dropped, or retargeted.

#### Scenario: A worker starts with pending rows
- **WHEN** the active Bridge generation starts its retry worker and version-3 pending rows exist
- **THEN** it restores exact installations, conversations, message content, target topic metadata, attempt counts, limits, and next-attempt times before processing them

#### Scenario: A retry succeeds
- **WHEN** a callback returns a target message reference matching the persisted target installation/conversation and repository operations succeed
- **THEN** the complete source-to-target mapping is queryable and only that exact retry row is no longer pending

#### Scenario: Completion persistence fails
- **WHEN** complete mapping persistence or exact retry-row cleanup fails after a target response is received
- **THEN** Bridge reports the persistence failure and keeps local state safe for idempotent recovery rather than silently claiming complete cleanup

#### Scenario: The same complete failure is enqueued again
- **WHEN** a failed source message with the same complete source and target scopes is added more than once
- **THEN** the durable queue contains one retry identity rather than parallel duplicate entries

#### Scenario: Equal native failures belong to different conversations or pairs
- **WHEN** equal platform/message ids are enqueued for different source conversations, target conversations, source installations, or target installations
- **THEN** the durable queue retains independent rows and completing one does not update or remove another

#### Scenario: Pre-send mapping already exists
- **WHEN** the worker finds an existing mapping for the retry's complete source and target scopes
- **THEN** it performs no provider call and removes only the corresponding retry row according to existing cleanup safety

#### Scenario: A different conversation is already mapped
- **WHEN** an equal native source id has a mapping in another conversation
- **THEN** the worker does not treat that mapping as completion of the current retry

#### Scenario: Version-2 retry lacks a conversation
- **WHEN** schema-version-3 preflight cannot derive both conversations for a pending retry
- **THEN** migration fails before actor activation and no retry is sent, archived, or removed automatically

### Requirement: One actor generation owns retry processing at a time
The retry worker SHALL be owned by its bridge actor generation and SHALL stop
before that generation's actor code, services, or dynamic library can be
retired. During reload, the old worker MUST stop before gated post-cutover
ingress can initialize the candidate worker against the same persistent queue.

#### Scenario: Actor-runtime reload succeeds with pending retries
- **WHEN** the old generation drains during reload while its bridge retry queue contains pending rows
- **THEN** the old worker stops, leaves pending rows durable, and only then may post-cutover ingress initialize the candidate worker and restore those rows

#### Scenario: Candidate preparation fails
- **WHEN** a reload candidate is rejected before publication
- **THEN** it never starts a retry worker and the active generation remains the sole retry owner

#### Scenario: Process shutdown occurs during a timer wait
- **WHEN** process or generation shutdown begins while the retry worker waits for its next queue check
- **THEN** the timer is cancelled on its executor, the worker thread exits, and no callback executes after bridge actor retirement

#### Scenario: Shutdown occurs during a resend
- **WHEN** shutdown begins while a resend callback is suspended on bot I/O
- **THEN** new attempts are rejected and shutdown waits for bounded cancellation or completion before releasing callback captures and actor code

### Requirement: Retry diagnostics are accurate and secret-safe
Bridge retry diagnostics SHALL distinguish disabled configuration, initialization failure, successful enqueue, failed attempt, unavailable exact installation, successful retry, outcome unknown, and terminal exhaustion. Diagnostics SHALL identify pair/installation direction where needed to distinguish concurrent accounts, and MUST NOT include message content, bot tokens, proxy credentials, signed URLs, or complete API responses.

#### Scenario: Enabled failed send is queued
- **WHEN** retry is enabled and a failed send is durably enqueued for one pair
- **THEN** the bridge reports the scoped queue identity and scheduling outcome and does not emit `消息发送失败且未启用重试`

#### Scenario: Retry attempt is logged
- **WHEN** the worker attempts or completes a resend
- **THEN** diagnostics include exact non-secret installation direction and attempt outcome without including the outgoing message or credentials

#### Scenario: Two equal native identities are diagnosed
- **WHEN** two pairs contain retries with equal platform and native message ids
- **THEN** diagnostics distinguish them by installation without exposing payload data
