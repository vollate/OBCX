## MODIFIED Requirements

### Requirement: Chat LLM uses typed group and topic sends only
`chat_llm` SHALL submit current QQ and non-topic Telegram replies through the common `message.send_group` typed adapter and current Telegram forum replies through the Telegram-owned `telegram.message.send_topic` adapter, both backed by `BotOperationGateway`. It SHALL preserve existing exact source-installation routing, text/reply segments, topic-id rules, and typed message-reference success checks. Chat LLM MUST NOT depend on an all-platform Bot operation client or OneBot-specific lookup/poke contracts merely to send a common group message.

#### Scenario: QQ group reply is sent
- **WHEN** current LLM processing produces a QQ group response
- **THEN** the common adapter submits the same segments through `message.send_group` to the exact source installation/group

#### Scenario: Telegram forum reply is sent
- **WHEN** the source Telegram event is a real forum-topic message with a valid topic id
- **THEN** the Telegram adapter submits `telegram.message.send_topic` with the same group/topic and reply segment

#### Scenario: Telegram message is not a forum topic
- **WHEN** Telegram carries a reply-chain thread id without `is_topic_message`
- **THEN** ordinary group send remains in use rather than inferring a topic

### Requirement: Chat LLM no longer receives or identifies live bots
`chat_llm` SHALL obtain the shared `BotOperationGateway` and use public common/Telegram typed adapters rather than `BotRegistry` or the former all-platform `BotOperationClient`. It MUST NOT include or accept live/provider bot interfaces, concrete bots, connection managers, process component catalogs, or provider executors. Command parsing SHALL receive validated surface/platform data and MUST NOT identify QQ using RTTI.

#### Scenario: Actor dependencies are inspected
- **WHEN** production headers and link dependencies are checked
- **THEN** no live bot, provider implementation, process catalog, or all-platform operation interface is required

#### Scenario: Command is parsed
- **WHEN** a current QQ or Telegram event becomes a parsed command
- **THEN** platform/topic semantics derive from validated source metadata and event fields rather than bot class identity

### Requirement: Chat LLM migration is covered by current actor tests
Automated tests SHALL cover QQ group reply, Telegram group/topic reply, non-topic reply chains, exact source routing, missing installation, typed failure, command completion, proactive send, reload, and shutdown through a fake gateway and real typed adapters. Tests MUST NOT add an unsupported real-platform implementation. Common/Telegram SDK-only compile coverage SHALL prove no OneBot-specific operation contract is required for this actor's egress.

#### Scenario: Installed actor suite runs
- **WHEN** rebuilt `chat_llm` is tested against a fresh modular SDK
- **THEN** current QQ/Telegram behavior passes without a fake live bot, old all-platform client, or provider response parsing

#### Scenario: Gateway returns malformed success
- **WHEN** fake gateway success data fails the expected typed result validation
- **THEN** Chat LLM follows its existing typed failure policy without logging provider payloads or recording a fabricated sent message
