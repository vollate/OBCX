# QQ/Telegram Bot Operation Boundary

This is intentionally a narrow egress API, not a universal messaging model.
Only the existing, testable `telegram.bot_api` and `onebot11.qq` adapters are
implemented. OneBot support never implies `qq.official`.

## Closed action matrix

| Action | Telegram | OneBot 11 | Current caller |
| --- | --- | --- | --- |
| `message.send_group` | yes | yes | Bridge, `chat_llm` |
| `message.delete` | yes | yes | Bridge |
| `telegram.message.send_topic` | yes | no | Bridge, `chat_llm` |
| `telegram.message.edit_text` | yes | no | Bridge |
| `telegram.media.send_photo` | yes | no | Bridge |
| `telegram.media.send_group_urls` | yes | no | Bridge |
| `telegram.media.send_group_uploads` | optional uploader | no | Bridge |
| `telegram.media.fetch_file` | yes | no | Bridge |
| `onebot11.group_member.get` | no | yes | Bridge |
| `onebot11.forward_message.get` | no | yes | Bridge |
| `onebot11.group_file.resolve` | no | yes | Bridge |
| `onebot11.private_file.resolve` | no | yes | Bridge |
| `onebot11.group.poke` | no | yes | Bridge |

Adding an action or platform requires a separate proposal and executable
adapter tests. Unused `IBot` methods are not capabilities.

## SDK and ownership

Actors use the installed headers:

- `core/bot_operation_contract.hpp`
- `core/bot_operation_client.hpp`

Every request names a `BotInstallationRef` containing the configured bot name
and exact surface. Group and message references retain that installation;
Telegram chat id and message id are separate values.

`BotOperationClient` is the only new actor service. The process dispatcher
owns wrappers around the existing `QQBot` and `TGBot`; only those wrappers see
legacy bot/provider interfaces and parse Telegram or OneBot response envelopes.
Tokens, clients, connection managers, executors, and Telegram tokenized file
URLs do not enter actor code.

`BotRegistry` and legacy interfaces remain process/runtime compatibility
infrastructure for external actors during this slice. The in-repository Bridge
and `chat_llm` actors no longer use them.

## Errors and retry safety

Operations return typed success or `BotOperationError`. Errors include
`DefinitelyNotSubmitted` or `PossiblySubmitted`:

- route, validation, unsupported-action, DNS/connect failure, proxy-tunnel
  failure, and TLS-handshake failure before HTTP request writing are definite;
- once HTTP request writing begins, malformed send success, timeout,
  disconnect, or response failure is possibly submitted unless proven
  otherwise.

Bridge automatically retries only a retryable `DefinitelyNotSubmitted` result.
A possibly submitted initial send creates no mapping and no retry row. A
possibly submitted retry is terminalized in the existing retry table by its
finite-attempt fields. There is no generic outbox or crash-safe reconciliation;
a process crash at the provider boundary remains deferred work.

## Bridge configuration migration

One Bridge actor/database supports exactly one pair in this slice:

```toml
[actors.bridge.config]
telegram_installation = "telegram_bot"
onebot11_installation = "qq_bot"
bridge_files_dir = "/tmp/bridge_files"
```

Both names must identify enabled root bot entries of type `telegram` and `qq`.
Messages, notices, and commands must carry the matching `source_bot`; no
platform-only fallback is used. Existing group/topic mappings and all Bridge
database tables remain unchanged. Multi-pair Bridge routing requires a later
scoped-mapping migration.

## Explicitly deferred

This change does not add typed ingress, Telegram checkpoint changes,
message-store migration, an ingress journal, a generic outbox, rate governance,
a blob gateway, mapping v2, private send, history, contacts, moderation,
reactions, polls, or adapters for unsupported platforms.
