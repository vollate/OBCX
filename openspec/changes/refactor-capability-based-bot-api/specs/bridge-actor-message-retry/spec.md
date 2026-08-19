## MODIFIED Requirements

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
