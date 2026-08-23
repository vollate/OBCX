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

## Bridge installation pairs and state migration

Bridge supports named, disjoint Telegram/OneBot installation pairs:

```toml
[actors.bridge.config]
bridge_files_dir = "/tmp/bridge_files"
legacy_state_pair = "primary"

[[actors.bridge.config.installation_pairs]]
id = "primary"
telegram_installation = "telegram_bot"
onebot11_installation = "qq_bot"

[[actors.bridge.config.installation_pairs]]
id = "secondary"
telegram_installation = "telegram_bot_2"
onebot11_installation = "qq_bot_2"

[[group_mappings.group_to_group]]
pair = "primary"
telegram_group_id = "-1001"
qq_group_id = "10001"
```

Each installation belongs to exactly one pair. Pair ids and installation ids
must be unique, and every mapping must name its pair when more than one exists.
Messages, notices, and commands carry `source_bot`; Bridge selects only that
bot's pair and never falls back by platform or fans out. The existing scalar
`telegram_installation`/`onebot11_installation` form remains valid for a single
pair and cannot be mixed with the collection form.

Bridge schema version 3 uses complete message identities consisting of exact
installation, platform, conversation, and native message id. QQ conversations
use `group:<id>` and Telegram conversations use `chat:<id>`; topic id remains
route metadata. Consequently equal native ids in two chats on one installation
remain independent for mappings, retries, media groups, replies, edits, and
recalls. User/sticker caches and heartbeats remain installation scoped.

An unversioned non-empty database is first assigned to the scalar pair, sole
named pair, or explicit `legacy_state_pair` as version 2. The startup-only
version-2-to-3 migration then resolves conversations from existing Message
Store identity and current or explicit historical routes. An exact Telegram
topic route uses stored thread evidence, while a group-to-group route remains
chat-wide even when a message carries forum-thread metadata. Strict mode rolls
back on unresolved state; explicit archive mode preserves unresolved
mapping/media rows in non-live tables that no production lookup can read.
Pending retries must migrate exactly. Typed generation preparation completes
this work before Bridge is registered for ingress and publishes the repository
only after commit. Stop OBCX and take a SQLite-consistent backup, including WAL
state, before migration. A candidate reload performs a read-only schema check
and cannot perform either incompatible migration; rollback requires restoring
both the old binary and pre-migration database snapshot.

## Explicitly deferred

This change does not add typed ingress, Telegram checkpoint changes, a Message
Store migration, an ingress journal, a generic outbox or reconciliation,
provider message lookup, rate governance, a blob gateway, private send,
history, contacts, moderation, reactions, polls, or adapters for unsupported
platforms. It also does not reconcile content that was already sent with a
wrong reply reference; that message must be explicitly removed and resent.
