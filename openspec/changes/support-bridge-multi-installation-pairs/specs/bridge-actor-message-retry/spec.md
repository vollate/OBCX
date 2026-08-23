## MODIFIED Requirements

### Requirement: Enabled bridge message retry is operational in actor mode
When `actors.bridge.config.enable_retry_queue` is true, the bridge actor SHALL create one message retry manager for its runtime generation, register every configured exact target installation, and provide that manager to the QQ and Telegram forwarding handlers before processing a send. The one generation-owned manager SHALL multiplex pairs without creating a worker per pair. An enabled generation MUST NOT report a failed send as retry-disabled.

#### Scenario: QQ-to-Telegram initial send fails
- **WHEN** forwarding from one exact OneBot installation to its paired Telegram installation fails definitely before submission and retry is enabled
- **THEN** the bridge persists and schedules one retry containing both installation ids, the outgoing message, source identity, target group, and target topic metadata

#### Scenario: Telegram-to-QQ initial send fails
- **WHEN** forwarding from one exact Telegram installation to its paired OneBot installation fails definitely before submission and retry is enabled
- **THEN** the bridge persists and schedules one retry containing both installation ids, the outgoing message, and source and target group identities

#### Scenario: Two pairs fail concurrently
- **WHEN** retryable sends fail for two configured pairs
- **THEN** the one generation worker retains two installation-scoped entries and neither replaces or dispatches the other

#### Scenario: Retry is explicitly disabled
- **WHEN** the same send failure occurs in a generation whose Bridge configuration explicitly disables retry
- **THEN** the bridge creates no retry worker or retry row and may report that retry is disabled

#### Scenario: Enabled retry cannot initialize
- **WHEN** retry is enabled but its worker, repository, exact-installation callbacks, or schema cannot be initialized
- **THEN** bridge forwarding fails with a retry-unavailable diagnostic and does not report that retry was disabled

### Requirement: Retry callbacks resend through process-owned bots
The bridge retry worker SHALL submit QQ group sends, Telegram group sends, and Telegram topic sends through `BotOperationClient` using the exact target installation persisted in each retry. It MUST NOT select a callback by platform, current pair order, or pair alias; resolve `BotRegistry`; retain `IBot` or a provider interface; use a concrete/dynamic cast; or invoke a connection manager. It SHALL accept a retry as successful only when the typed result contains a valid target message reference for the persisted installation and SHALL distinguish definitely retryable failure, terminal failure, and possibly submitted outcome.

#### Scenario: Telegram group retry succeeds
- **WHEN** a QQ-origin retry names an enabled Telegram target installation and group without a positive topic id and typed dispatch returns a matching target message reference
- **THEN** the worker records that installation/message identity and completes only that retry

#### Scenario: Telegram topic retry succeeds
- **WHEN** a QQ-origin retry names an enabled Telegram target installation and positive topic id and typed dispatch returns a matching target message reference
- **THEN** the worker uses `telegram.message.send_topic`, records that identity, and completes only that retry

#### Scenario: QQ group retry succeeds
- **WHEN** a Telegram-origin retry names an enabled `onebot11.qq` target installation and typed dispatch returns a matching target message reference
- **THEN** the worker records that installation/message identity and completes only that retry

#### Scenario: Persisted target installation is unavailable
- **WHEN** a restored retry names an installation that is no longer dispatched by the operation client
- **THEN** the worker reports the typed unavailable route and never sends through another installation on the same platform

#### Scenario: Target failure is definitely retryable
- **WHEN** exact dispatch reports a retryable failure proven to occur before provider submission
- **THEN** the worker records an unsuccessful attempt and retains the scoped retry while attempts remain

#### Scenario: Target outcome may have been submitted
- **WHEN** exact dispatch reports `PossiblySubmitted`
- **THEN** the worker stops automatic attempts for that entry according to the existing finite-attempt policy and reports an outcome-unknown diagnostic

### Requirement: Persistent retry state and message mappings remain consistent
The bridge SHALL persist retry identity by source installation, source platform/message id, target installation, and target platform. It SHALL preserve pending attempts across process restart and actor-runtime reload. After a successful resend, it SHALL persist an installation-scoped source-to-target message mapping and remove only the corresponding scoped retry row.

#### Scenario: The same scoped failure is enqueued again
- **WHEN** a failed source message with the same source installation/platform/message id and target installation/platform is added more than once
- **THEN** the durable queue contains one retry identity rather than parallel duplicate entries

#### Scenario: Equal native failures belong to different pairs
- **WHEN** equal platform/message ids are enqueued for different source or target installations
- **THEN** the durable queue retains independent rows and completing one does not update or remove the other

#### Scenario: A worker starts with pending rows
- **WHEN** the active bridge generation starts its retry worker and durable pending rows exist
- **THEN** it restores their exact installations, message content, target metadata, attempt counts, limits, and next-attempt times before processing them

#### Scenario: A retry succeeds
- **WHEN** a callback returns a target message reference for the persisted target installation and repository operations succeed
- **THEN** the exact source-to-target mapping is queryable and that retry row is no longer pending

#### Scenario: Completion persistence fails
- **WHEN** scoped mapping persistence or retry-row cleanup fails after a target response is received
- **THEN** the bridge reports the persistence failure and keeps local state safe for idempotent recovery rather than silently claiming complete cleanup

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
