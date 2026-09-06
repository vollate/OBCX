## MODIFIED Requirements

### Requirement: Retry callbacks resend through process-owned bots
The Bridge retry worker SHALL use common group-send and Telegram topic-send typed SDK adapters backed by `BotOperationGateway`, always targeting the exact installation and conversation persisted in each retry. It MUST NOT select a callback or conversation by platform, current route, pair ordering/alias, or native message id alone; access a live-bot registry or provider interface; cast a bot; invoke a connection manager; or introduce provider message lookup. A retry SHALL succeed only from a validated typed result matching its persisted target installation/conversation. The worker SHALL retain its existing distinctions between definitely retryable failure, terminal failure, and possibly submitted outcome.

#### Scenario: Telegram group retry succeeds
- **WHEN** a QQ-origin retry names an enabled Telegram installation/chat without a positive topic id and the common typed adapter returns a matching message reference
- **THEN** the worker records that complete target identity and completes only that conversation-scoped retry

#### Scenario: Telegram topic retry succeeds
- **WHEN** a QQ-origin retry names a Telegram chat and positive topic id and the Telegram adapter returns a matching message reference
- **THEN** it uses `telegram.message.send_topic`, records the containing chat and native message id, and completes only that retry

#### Scenario: QQ group retry succeeds
- **WHEN** a Telegram-origin retry names an enabled OneBot installation/group and the common adapter returns a matching message reference
- **THEN** the worker records that complete target identity and completes only that retry

#### Scenario: Persisted target installation or conversation is unavailable
- **WHEN** a restored retry no longer has an exact configured target route
- **THEN** the worker reports the typed unavailable route and never substitutes another installation, group, or chat

#### Scenario: Target failure is definitely retryable
- **WHEN** exact dispatch reports a retryable failure proven to precede provider submission
- **THEN** the worker records an unsuccessful attempt and retains the same scoped retry while attempts remain

#### Scenario: Target outcome may have been submitted
- **WHEN** dispatch or typed result decoding reports `PossiblySubmitted`
- **THEN** the worker stops automatic attempts according to its existing finite-attempt policy and reports a secret-safe uncertain outcome

#### Scenario: Existing retry state is restored after SDK migration
- **WHEN** rebuilt Bridge restores schema-3 retry rows
- **THEN** source/target installations, conversations, topic, payload, and attempt state remain unchanged and no row is archived, retargeted, or migrated merely because of the gateway change
