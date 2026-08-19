## Why

The only provider integrations that can be exercised end to end today are the existing OneBot 11/QQ and Telegram Bot API implementations. Replacing the whole researched messaging stack at once would combine unrelated ingress, persistence, reliability, media, and future-platform work that cannot be validated, so this change must be limited to the QQ/Telegram bot calls already made by production actors.

## What Changes

- Add a small actor-safe bot operation contract for the exact `telegram.bot_api` and `onebot11.qq` installations already configured by OBCX. It contains scoped installation/group/message references, the existing message-segment payload, typed success values, and typed provider/transport failures.
- Freeze the first operation allow-list to current call sites only: QQ and Telegram group send/delete; Telegram topic send, text edit, photo/media-group send, upload, and file fetch; and OneBot group-member lookup, forwarded-message lookup, group/private file resolution, and group poke.
- Add a process-owned dispatcher that routes by configured bot name, reports the finite supported-action set for each of the two wrappers, calls the existing bot implementations, and parses their JSON responses before returning to actors.
- Migrate the existing Bridge and `chat_llm` actors from `BotRegistry`, `IBot`, `IQQBot`, `ITelegramBot`, and runtime casts to the narrow operation client while retaining their current QQ/Telegram behavior.
- Require Bridge to name one explicit Telegram installation and one explicit OneBot installation and reject source events that do not belong to that pair. Multi-pair Bridge routing is deferred so the existing mapping schema does not need to change in this refactor.
- Change Bridge message retry callbacks to use typed send results and automatically retry only failures known to occur before provider submission. An uncertain send is reported but not blindly retried; no generic outbox is introduced.
- Keep the current raw ingress, message-store contract, Bridge mapping/retry tables, media caches and transformations, command pipeline, bot transports, and process lifecycle. `BotRegistry` and the legacy bot interfaces remain available as compatibility infrastructure outside the two migrated actors.
- **BREAKING**: Bridge configuration must provide the exact `telegram_installation` and `onebot11_installation`; Bridge and `chat_llm` no longer receive live bot objects.
- Do not add adapters, contracts, probes, or tests for Discord, official QQ, WeChat, WeCom, Lark, DingTalk, Matrix, X, or any other unsupported platform. Unused `IBot` methods and researched capabilities are explicitly deferred.

## Capabilities

### New Capabilities

- `qq-telegram-bot-contract`: Data-only request/result/error contracts and the closed operation allow-list required by current QQ/Telegram actor call sites.
- `bot-operation-dispatch`: Exact-installation routing and process wrappers over the existing Telegram and OneBot bot implementations.
- `bridge-capability-forwarding`: Existing Bridge forwarding, media, mutation, command, and mapping behavior through the narrow operation client.
- `chat-llm-capability-egress`: Existing `chat_llm` QQ group and Telegram group/topic replies through the narrow operation client.

### Modified Capabilities

- `bridge-actor-message-retry`: Existing QQ/Telegram retry callbacks use typed sends and distinguish definitely retryable failures from uncertain provider submission.

## Impact

- Public SDK headers for the finite QQ/Telegram operation DTOs and actor-safe client.
- Process runtime wiring around existing `QQBot`, `TGBot`, and `BotRegistry`; no transport rewrite or new provider dependency.
- `local_actor/obcx-actor-bridge` handler signatures, explicit installation config, direct sends, media calls, commands, retry callbacks, and tests; existing database schemas remain unchanged.
- `local_actor/chat_llm` routing, command parsing, sends, and tests.
- Existing raw event/message-store pipelines and all unsupported platforms are unaffected.
