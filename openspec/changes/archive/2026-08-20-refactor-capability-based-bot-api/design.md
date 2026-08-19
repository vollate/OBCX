## Context

OBCX currently has two implemented and testable provider paths: `QQBot` over OneBot 11 and `TGBot` over Telegram Bot API. The process owns their connections and credentials, but actors obtain `BotRegistry`, resolve `IBot`, dynamically cast provider interfaces, call network methods, and parse opaque JSON. Production actor users are `obcx-actor-bridge` and `chat_llm`.

The compared `origin/refactor/universal-bot-api` branch has the same Git tree as `origin/main`; it provides no additional universal API implementation. The capability research is useful as a taxonomy, but implementing its full Bridge MVP would also require new typed ingress, a message-store migration, durable journals/outboxes, rate governance, a blob gateway, and fully scoped mapping migrations. Those concerns are not prerequisites for removing the current actors' live-bot dependency and are intentionally deferred.

The current production call inventory is the scope authority. A capability absent from this inventory is not part of this change even if it appears in `IBot` or the research documents.

## Goals / Non-Goals

**Goals:**

- Replace live-bot access in the existing Bridge and `chat_llm` actors with one data-only operation client.
- Support only the current `telegram.bot_api` and `onebot11.qq` implementations and only operations exercised by current actor code.
- Route every migrated call by exact configured bot name and keep OneBot distinct from official QQ.
- Parse provider success/error envelopes in process wrappers so actors no longer extract message ids or status from send/mutation responses.
- Preserve current forwarding, media, command, retry, reload, shutdown, and mapping-operation behavior.
- Use existing mock transports and actor suites as the acceptance boundary.

**Non-Goals:**

- Supporting or defining APIs for any other platform, including official QQ.
- Implementing the researched 26-component Bridge MVP or freezing a future universal messaging model.
- Replacing raw `RawMessageEvent`/`RawNoticeEvent`, changing Telegram polling checkpoints, or migrating message-store payloads.
- Adding an ingress journal, generic outbox, operation reconciliation store, rate governor, credential service, or media blob gateway.
- Changing Bridge message, media-group, retry, user, sticker, heartbeat, or message-store database schemas.
- Supporting multiple Telegram/OneBot installation pairs in one Bridge actor/database.
- Rewriting existing transports, media conversion/cache code, or arbitrary URL download logic.
- Migrating process-internal command-catalog publication or deleting `IBot`/`BotRegistry` globally.
- Exposing currently unused `IBot` actions such as private send, history, contacts, moderation, membership lists, requests, credentials, or cookies.

## Decisions

### 1. Freeze a closed operation matrix from current call sites

The public contract contains only the following action ids. Adding another action, surface, or caller requires a later reviewed change with tests for its actual adapter.

| Action id | Telegram Bot API | OneBot 11/QQ | Current caller |
| --- | --- | --- | --- |
| `message.send_group` | yes | yes | Bridge, `chat_llm` |
| `message.delete` | yes | yes | Bridge recall/removal |
| `telegram.message.send_topic` | yes | no | Bridge, `chat_llm` |
| `telegram.message.edit_text` | yes | no | Bridge edit propagation |
| `telegram.media.send_photo` | yes | no | Bridge cached-image path |
| `telegram.media.send_group_urls` | yes | no | Bridge media-group path |
| `telegram.media.send_group_uploads` | yes | no | Bridge multipart fallback |
| `telegram.media.fetch_file` | yes | no | Bridge Telegram-to-QQ media path |
| `onebot11.group_member.get` | no | yes | Bridge sender/mention formatting |
| `onebot11.forward_message.get` | no | yes | Bridge merged-forward formatting |
| `onebot11.group_file.resolve` | no | yes | Bridge file segment handling |
| `onebot11.private_file.resolve` | no | yes | Bridge file segment handling |
| `onebot11.group.poke` | no | yes | Bridge `/poke` command |

`message.send_group` deliberately reuses the existing `common::Message` segment vector. Telegram entity, media-group, upload, topic, and OneBot query fields remain explicitly provider-specific request types. This avoids introducing and migrating a speculative portable content tree.

The current process-only command catalog may continue to use `ITelegramBot::set_commands`; it is not an actor call and is outside this matrix. Bridge's existing `error_notify` use is replaced with an ordinary typed group send rather than adding another action.

**Rejected alternatives:** implementing every `IBot` method would preserve the oversized OneBot-shaped API; implementing all research components would exceed the testable boundary; reducing the first slice to text send only would leave Bridge dependent on live bots for its already-tested media and command paths.

### 2. Use small scoped DTOs rather than the full messaging domain model

The SDK adds data-only values under a bot-operation namespace:

- `BotInstallationRef { installation_id, surface }`, where surface is exactly `telegram.bot_api` or `onebot11.qq`;
- `GroupTarget { installation, native_group_id }` and `TelegramTopicTarget { group, topic_id }`;
- `BotMessageRef { group, native_message_id }` so Telegram chat id and message id are never colon-encoded;
- one request/result struct for each action in the table;
- `BotOperationError` with a stable code, redacted message, optional provider code/retry delay, retryability, and `DefinitelyNotSubmitted` or `PossiblySubmitted` safety;
- `BotOperationResult<T>` containing either the typed value or typed error.

Successful sends return one or more `BotMessageRef` values as appropriate. Delete and edit return typed status. OneBot member/file/forward responses expose only the fields current Bridge code consumes; the complex forwarded-message node body may remain validated OneBot-namespaced JSON because this change does not define a cross-platform forward-node model. Telegram fetched/upload media bytes are bounded data values; tokens, tokenized URLs, clients, streams, and unrestricted paths do not cross the client boundary.

The DTOs have deterministic JSON conversion because actor packages build independently against the installed SDK, but this change does not introduce a generic operation envelope, durable operation id, capability constraints, delivery/read states, or schema for future platforms.

**Rejected alternatives:** continuing to return provider response strings would keep parsing and error ambiguity in actors; introducing the broad `obcx::messaging` model would force ingress and storage migration; passing bot pointers inside a nominally typed request would not change the ownership boundary.

### 3. Wrap existing bots with a minimal process dispatcher

A process-owned `BotOperationRuntime` is built around the existing process `BotRegistry` registrations:

```text
Bridge / chat_llm
  -> BotOperationClient (data only)
  -> QQTelegramOperationDispatcher
  -> exact installation wrapper
  -> existing QQBot or TGBot
```

Registration uses the configured bot name as `installation_id`; bot type determines the exact surface. The runtime rejects duplicate ids and unsupported surfaces. Lookup never accepts only `qq`, `telegram`, or a surface when an installation id is required.

Each wrapper publishes a static supported-action set derived from its concrete implementation and optional interface availability. There is no dynamic entitlement model, target-aware snapshot version, probe scheduler, or generic capability directory in this slice. Unsupported actions return `UnsupportedAction` before provider I/O. `onebot11.qq` never advertises `qq.official`.

Only wrapper implementation code may access `IBot`, `IQQBot`, `ITelegramBot`, `ITelegramMediaGroupUploader`, connection-backed methods, or provider response JSON. The actor-facing client exposes `execute` overloads and `supported_actions(installation)` as values. The process retains shared bot ownership for the duration of each call using the lifecycle already provided by `BotRegistry`; this change adds no worker thread or endpoint supervisor.

Provider result handling is conservative:

- route, validation, and unsupported failures are `DefinitelyNotSubmitted`;
- DNS/connect, proxy-tunnel, and TLS-handshake failures before HTTP request writing are retryable `DefinitelyNotSubmitted` transport failures;
- explicit provider rejection with no side effect is typed from Telegram `{ok:false}` or OneBot failed `status/retcode`;
- a valid send parses required message ids into scoped references;
- a malformed nominal success, timeout, disconnect, or exception after a side-effecting call begins defaults to `PossiblySubmitted` unless the existing transport can prove no write occurred;
- no actor receives an empty/synthetic success from the new client.

**Rejected alternatives:** a new transport stack would duplicate working code; actor-side adapters would retain credentials and live objects; broad runtime outbox/rate/media services are separate reliability work and are not needed for this boundary slice.

### 4. Keep compatibility infrastructure while migrating only current actors

`BotRegistry`, `IBot`, and provider interfaces remain process infrastructure and may remain registered for compatibility during this change. This avoids breaking unknown external actors while the new client proves itself. Architecture tests are scoped to the two in-repository migrated actors: their production sources and exported link dependencies may not obtain or cast live bots after cutover.

The same process `BotOperationRuntime` and client are supplied to every actor generation. Existing bot creation, command-catalog publication, reload fingerprinting, transport startup, and shutdown ordering remain authoritative. Validation-only mode may construct static DTO/config validation but must not add network calls or new persistent state.

A later proposal may remove actor-visible `BotRegistry` after external users have migrated. This change does not claim that global cleanup.

### 5. Route Bridge through one explicit QQ/Telegram installation pair

Bridge actor configuration gains required `telegram_installation` and `onebot11_installation` fields. Validation checks that both enabled root bot entries exist, have the expected surface, are different ids, and are supported by the operation runtime. A Bridge actor owns exactly one pair in this slice. Every source message/notice/command must carry `source_bot` matching the configured side; missing or mismatched identity fails without platform fallback.

Group and topic mappings continue to contain their existing conversation ids and modes. Because one Bridge actor/database is restricted to one installation pair, existing mapping and retry tables remain unchanged. This proposal neither fixes nor expands their legacy identity model; multi-pair support requires the separately testable scoped-mapping migration previously considered.

All handler objects receive the value client plus the two installation refs rather than `IBot&` parameters. Existing raw event conversion, message formatting, media-group buffering, cache files, ffmpeg work, URL validation, command completion, heartbeat storage, and the single-owner direct mapping write remain in place. Events outside configured mappings, disabled directions, loop suppression, and accepted deferred media-group items complete as no-ops; attempted delivery failures remain explicit typed failures.

**Rejected alternatives:** inferring the only bot by platform perpetuates ambiguity; adding installation columns to every table would turn this boundary slice into a persistence migration; one pair per individual mapping would advertise multi-account behavior the legacy keys cannot safely support.

### 6. Move only bot-owned media calls, not the whole media pipeline

The client covers the bot-owned calls currently reached through Telegram and OneBot interfaces. Telegram file resolution and authenticated download are combined as `telegram.media.fetch_file`, returning bounded bytes plus metadata so tokenized URLs stay process-side. Telegram media sends accept the same bounded URL/upload/entity inputs currently produced by Bridge. OneBot file-resolution actions return the provider URL currently consumed by Bridge.

Bridge continues to own its existing cache layout, temporary paths, image URL probing/download, photo normalization, sticker/animation conversion, and cleanup. No `MediaBlobGateway`, lease model, content-addressed store, or portable media policy is added.

**Rejected alternatives:** moving all media processing now would be a second major refactor; leaving Telegram tokenized URL resolution in the actor would preserve a credential-bearing provider seam.

### 7. Preserve Bridge retry storage and add only typed retry safety

The existing generation-owned retry manager and database rows remain. Its QQ and Telegram callbacks submit `message.send_group` or `telegram.message.send_topic` through the exact configured installation and consume `BotOperationResult<SendMessageResult>`.

The callback outcome is one of completed, definitely retryable failure, terminal failure, or outcome unknown. Initial forwarding and retry workers enqueue/reschedule only a definitely-not-submitted retryable failure. `PossiblySubmitted` transitions the current item to a non-runnable terminal/diagnostic disposition using the existing finite-attempt policy; it is never automatically sent again. This reduces duplicate risk without claiming crash-safe reconciliation.

There is deliberately no operation id, request fingerprint, generic outbox, or persisted provider-handoff marker. A process crash at the provider boundary remains an acknowledged limitation for a later reliability change.

### 8. Migrate `chat_llm` by source installation, not bot RTTI

`chat_llm` derives platform and exact installation from `MessageEnvelope`/`CommandInvocation`, validates that the surface is one of the two allowed surfaces, and carries the installation value through processing. Group replies use `message.send_group`; Telegram forum replies use `telegram.message.send_topic`. Existing reply segments, conversation persistence, proactive behavior, command completion, and LLM logic remain unchanged.

`CommandParser` receives the source surface/platform as data rather than detecting QQ through `dynamic_cast<IQQBot*>`. A send failure is translated to the actor's existing retryable/terminal result policy; the actor does not parse provider JSON.

### 9. Test only executable QQ/Telegram behavior

Acceptance uses existing fake bots/mock transports and isolated actor databases:

- contract round-trip and validation tests for every DTO in the closed matrix;
- dispatcher tests for exact installation, wrong surface, unsupported action, valid Telegram/OneBot success, provider rejection, malformed response, and uncertain transport failure;
- existing Bridge simulations for both directions, topics, replies, media groups/uploads, edits/removals, commands, retries, mapping operation counts, reload, and shutdown;
- existing `chat_llm` QQ group and Telegram group/topic response tests;
- architecture checks limited to production Bridge and `chat_llm` sources/link interfaces.

No test or implementation branch is required for an unsupported platform. Production credentials and production databases are never used.

## Risks / Trade-offs

- **[The contract is intentionally not universal]** → Name surfaces/actions explicitly and require a separate tested change before adding another platform or operation.
- **[Legacy raw events and OneBot-shaped message segments remain]** → Treat this as an egress ownership refactor only; ingress/domain normalization is a later change.
- **[BotRegistry remains actor-compatible globally]** → Gate the two migrated in-repository actors now and remove the compatibility service only after external migration evidence exists.
- **[Bridge remains single-pair and legacy mappings remain under-scoped]** → Reject multiple configured pairs and do not claim multi-account Bridge support in this slice.
- **[No outbox can prove outcome across a process crash]** → Conservatively stop automatic retry for observable uncertain results and document the remaining crash window.
- **[Bounded media bytes cross the data client]** → Retain current size limits and tests; introduce a blob gateway only if measured memory/security needs justify it.
- **[Provider-specific JSON remains for OneBot forwarded nodes]** → Keep it namespaced and validated; do not pretend it is portable.
- **[Existing Telegram legacy stubs still exist]** → They are not advertised or called through the new client; global interface cleanup is deferred.

## Migration Plan

1. Capture current Bridge and `chat_llm` behavior and freeze the operation inventory with a source-scan test.
2. Add the finite DTOs, operation client, exact-installation runtime, and fake-wrapper tests without changing actors.
3. Implement Telegram and OneBot wrappers for only the matrix actions and wire the shared client into runtime generations alongside compatibility services.
4. Add explicit Bridge installation-pair config, migrate direct sends/mutations/provider lookups/media calls/commands, then migrate retry callbacks.
5. Migrate `chat_llm` routing and group/topic sends and remove its bot RTTI/registry dependency.
6. Run root and standalone actor suites, reload/shutdown tests, architecture checks, and strict OpenSpec validation.

Rollback deploys the preceding binaries and removes the two new Bridge config keys. No database rollback or data projection is required because this change adds no durable schema. Existing bot interfaces and `BotRegistry` remain available during the compatibility window.

## Open Questions

None for this slice. The operation matrix is frozen from current production call sites; any expansion requires another proposal with adapter-specific tests.
