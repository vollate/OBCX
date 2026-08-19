# chat-llm-capability-egress Specification

## Purpose
Define exact-installation QQ and Telegram egress for `chat_llm` through the data-only bot-operation client.

## Requirements
### Requirement: Chat LLM routes by the exact source installation
`chat_llm` SHALL derive its outbound `BotInstallationRef` from `MessageEnvelope.source_bot` or `CommandInvocation.source_bot` plus the matching current source platform. It MUST reject a missing installation, a mismatch between `qq` and `onebot11.qq`, a mismatch between `telegram` and `telegram.bot_api`, or any unsupported platform instead of resolving the first bot for a platform.

#### Scenario: QQ message triggers a reply
- **WHEN** a QQ message names an enabled OneBot installation
- **THEN** `chat_llm` routes its response to that exact `onebot11.qq` installation and source group

#### Scenario: Telegram command triggers a reply
- **WHEN** a Telegram command names an enabled Telegram installation
- **THEN** `chat_llm` routes its response to that exact `telegram.bot_api` installation

#### Scenario: Source bot is missing
- **WHEN** a message or command has no `source_bot`
- **THEN** the actor returns an actionable non-provider route failure and sends nothing

### Requirement: Chat LLM uses typed group and topic sends only
`chat_llm` SHALL submit current QQ and non-topic Telegram replies through `message.send_group` and current Telegram forum replies through `telegram.message.send_topic`. It SHALL preserve the existing text/reply segments and topic id behavior and SHALL treat a send as successful only when the typed result contains a valid scoped message reference.

#### Scenario: QQ group reply is sent
- **WHEN** current LLM processing produces a QQ group response
- **THEN** the actor submits the same message segments through `message.send_group`

#### Scenario: Telegram forum reply is sent
- **WHEN** the triggering Telegram event is a real forum-topic message with a valid topic id
- **THEN** the actor submits `telegram.message.send_topic` with that group/topic and preserves the existing reply segment

#### Scenario: Telegram message is not a forum topic
- **WHEN** Telegram carries a reply-chain `message_thread_id` without `is_topic_message`
- **THEN** the actor continues to use ordinary group send rather than treating the thread id as a topic

### Requirement: Chat LLM no longer receives or identifies live bots
`chat_llm` production code SHALL obtain `BotOperationClient` rather than `BotRegistry` and MUST NOT include or accept `IBot`, `IQQBot`, `ITelegramBot`, concrete bots, or connection managers. Command parsing SHALL receive the source platform/surface as data and MUST NOT detect QQ with RTTI.

#### Scenario: Actor dependencies are inspected
- **WHEN** `chat_llm` production sources and exported link dependencies are scanned
- **THEN** no live bot registry, bot/provider interface, concrete bot, connection-manager, or dynamic-cast dependency remains

#### Scenario: Command is parsed
- **WHEN** a current QQ or Telegram message is converted to `ParsedCommand`
- **THEN** platform and topic semantics come from validated source data and event fields rather than bot class identity

### Requirement: Existing LLM and conversation behavior is preserved
The migration SHALL leave trigger detection, command handling, LLM calls, proactive sends, conversation state, message persistence, reply recording, command completion, reload, and shutdown semantics unchanged except for typed outbound failure handling. Provider response JSON MUST NOT enter `chat_llm`.

#### Scenario: Typed send fails
- **WHEN** the operation client returns a typed route, provider, or transport error
- **THEN** `chat_llm` maps it to its existing actor failure policy without parsing or logging a complete provider response

#### Scenario: Proactive send succeeds
- **WHEN** current proactive logic chooses `send_message`
- **THEN** it uses the stored exact installation and current group/topic route and records the local response as before

### Requirement: Chat LLM migration is covered by current actor tests
Automated tests SHALL cover QQ group reply, Telegram group reply, Telegram forum-topic reply, non-topic reply-chain handling, exact source-bot routing, missing installation, typed send failure, command completion, proactive send, reload, and shutdown using a fake operation client. Tests MUST NOT add an unsupported-platform fixture.

#### Scenario: Installed actor suite runs
- **WHEN** `chat_llm` is built and tested against the installed SDK
- **THEN** its current QQ/Telegram behavior passes without a fake `IBot` or `BotRegistry` service
