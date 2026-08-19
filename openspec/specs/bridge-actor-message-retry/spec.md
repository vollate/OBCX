# bridge-actor-message-retry Specification

## Purpose
TBD - created by archiving change restore-bridge-actor-message-retry. Update Purpose after archive.
## Requirements
### Requirement: Enabled bridge message retry is operational in actor mode
When `actors.bridge.config.enable_retry_queue` is true, the bridge actor SHALL
create one message retry manager for its runtime generation, register both
supported target-platform callbacks, and provide that manager to the QQ and
Telegram forwarding handlers before processing a send. An enabled generation
MUST NOT report a failed send as retry-disabled.

#### Scenario: QQ-to-Telegram initial send fails
- **WHEN** actor-mode QQ-to-Telegram forwarding fails before a target message id is obtained and retry is enabled
- **THEN** the bridge persists and schedules one Telegram-target retry containing the outgoing message, source identity, target group, and target topic metadata

#### Scenario: Telegram-to-QQ initial send fails
- **WHEN** actor-mode Telegram-to-QQ forwarding fails before a target message id is obtained and retry is enabled
- **THEN** the bridge persists and schedules one QQ-target retry containing the outgoing message and source and target group identities

#### Scenario: Retry is explicitly disabled
- **WHEN** the same send failure occurs in a generation whose bridge configuration explicitly disables retry
- **THEN** the bridge creates no retry worker or retry row and may report that retry is disabled

#### Scenario: Enabled retry cannot initialize
- **WHEN** retry is enabled but its worker, repository, or callbacks cannot be initialized
- **THEN** bridge forwarding fails with a retry-unavailable diagnostic and does not report that retry was disabled

### Requirement: Retry callbacks resend through process-owned bots
The bridge retry worker SHALL submit QQ group sends, Telegram group sends, and Telegram topic sends through `BotOperationClient` using the single exact target installation configured for the Bridge actor. It MUST NOT resolve `BotRegistry`, retain `IBot` or a provider interface, use a concrete/dynamic cast, or invoke a connection manager. It SHALL accept a retry as successful only when the typed result contains a valid target message reference and SHALL distinguish definitely retryable failure, terminal failure, and possibly submitted outcome.

#### Scenario: Telegram group retry succeeds
- **WHEN** a QQ-origin retry targets the configured Telegram installation and group without a positive topic id and typed dispatch returns a target message reference
- **THEN** the worker records that message id and completes the retry

#### Scenario: Telegram topic retry succeeds
- **WHEN** a QQ-origin retry has a positive Telegram topic id and typed dispatch returns a target message reference
- **THEN** the worker uses `telegram.message.send_topic`, records that message id, and completes the retry

#### Scenario: QQ group retry succeeds
- **WHEN** a Telegram-origin retry targets the configured `onebot11.qq` installation and typed dispatch returns a target message reference
- **THEN** the worker records that message id and completes the retry

#### Scenario: Target failure is definitely retryable
- **WHEN** routing or the wrapper reports a retryable failure proven to occur before provider submission
- **THEN** the worker records an unsuccessful attempt and retains the retry while attempts remain

#### Scenario: Target outcome may have been submitted
- **WHEN** typed dispatch reports `PossiblySubmitted`
- **THEN** the worker stops automatic attempts for that entry according to the existing finite-attempt policy and reports an outcome-unknown diagnostic

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
The bridge SHALL persist retry identity by source platform, source message id,
and target platform. It SHALL preserve pending attempts across process restart
and actor-runtime reload. After a successful resend, it SHALL persist the
source-to-target message mapping and remove the corresponding retry row.

#### Scenario: The same failure is enqueued again
- **WHEN** a failed source message with the same source platform, source message id, and target platform is added more than once
- **THEN** the durable queue contains one retry identity rather than parallel duplicate entries

#### Scenario: A worker starts with pending rows
- **WHEN** the active bridge generation starts its retry worker and durable pending rows exist
- **THEN** it restores their message content, target metadata, attempt counts, limits, and next-attempt times before processing them

#### Scenario: A retry succeeds
- **WHEN** a callback returns a valid target message id and repository operations succeed
- **THEN** the source-to-target mapping is queryable and the retry row is no longer pending

#### Scenario: Completion persistence fails
- **WHEN** mapping persistence or retry-row cleanup fails after a target response is received
- **THEN** the bridge reports the persistence failure and keeps local state safe for idempotent recovery rather than silently claiming complete cleanup

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
Bridge retry diagnostics SHALL distinguish disabled configuration,
initialization failure, successful enqueue, failed attempt, successful retry,
and terminal exhaustion. Diagnostics MUST NOT include message content, bot
tokens, proxy credentials, or complete API responses.

#### Scenario: Enabled failed send is queued
- **WHEN** retry is enabled and a failed send is durably enqueued
- **THEN** the bridge reports the queue identity and scheduling outcome and does not emit `消息发送失败且未启用重试`

#### Scenario: Retry attempt is logged
- **WHEN** the worker attempts or completes a resend
- **THEN** diagnostics include platform direction and attempt outcome without including the outgoing message or credentials
