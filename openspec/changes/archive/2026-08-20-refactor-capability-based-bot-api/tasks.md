## 1. Freeze the testable QQ/Telegram scope

- [x] 1.1 Add a source-inventory test that enumerates the production Bridge and `chat_llm` bot calls and fails if an action outside the approved 13-action matrix is added
- [x] 1.2 Add or confirm mock coverage for current Telegram and OneBot group send/delete success, explicit provider failure, malformed response, and side-effect exception behavior
- [x] 1.3 Capture existing Bridge bidirectional text/reply/media/edit/removal/command/retry behavior and direct-mapping operation counts with isolated databases
- [x] 1.4 Capture existing `chat_llm` QQ group, Telegram group, Telegram forum-topic, command, proactive-send, reload, and shutdown behavior

## 2. Add the finite bot-operation contract

- [x] 2.1 Add installable SDK headers for `BotInstallationRef`, `GroupTarget`, `TelegramTopicTarget`, `BotMessageRef`, the 13 action ids, `SubmissionSafety`, `BotOperationError`, and `BotOperationResult<T>`
- [x] 2.2 Add typed common group-send/delete and Telegram topic-send/text-edit request and result values using the existing `common::Message` payload
- [x] 2.3 Add bounded Telegram photo, media-group URL, media-group upload, entity, and file-fetch request/result values required by Bridge
- [x] 2.4 Add typed OneBot group-member, forwarded-message, group/private-file resolution, and group-poke request/result values required by Bridge
- [x] 2.5 Add deterministic JSON conversion, validation, equality, redaction, wrong-surface, missing-route, topic-id, and media-bound tests for every public value
- [x] 2.6 Export the new headers through the root install/package configuration and verify a standalone SDK consumer can compile and round-trip them

## 3. Implement exact-installation dispatch over existing bots

- [x] 3.1 Implement the data-only `BotOperationClient` and process `QQTelegramOperationDispatcher` with duplicate registration rejection and exact installation/surface/target validation
- [x] 3.2 Register wrappers for enabled existing `QQBot` and `TGBot` instances by configured bot name without adding a transport, probe, worker, or persistent store
- [x] 3.3 Implement shared Telegram `{ok,result}` and OneBot `status/retcode/data/echo` parsing into redacted typed successes/errors and conservative submission safety
- [x] 3.4 Implement typed QQ and Telegram `message.send_group` and `message.delete`, including required scoped message-id extraction
- [x] 3.5 Implement Telegram topic send and text edit with separate group/topic/chat/message fields
- [x] 3.6 Implement Telegram photo, URL media-group, multipart media-group, and bounded authenticated file-fetch actions using existing bot interfaces internally
- [x] 3.7 Implement OneBot group-member, forwarded-message, group/private-file resolution, and group-poke actions using existing bot interfaces internally
- [x] 3.8 Publish only the closed per-wrapper action sets, including conditional multipart upload support and no `qq.official` alias
- [x] 3.9 Inject the same operation client into runtime generations alongside compatibility `BotRegistry` and preserve validation-only, reload, and shutdown behavior
- [x] 3.10 Add dispatcher/wrapper conformance tests for every advertised action, exact route, wrong surface, unsupported action, provider rejection, malformed response, byte bound, and possibly-submitted exception

## 4. Migrate Bridge routing and direct sends

- [x] 4.1 Add required `telegram_installation` and `onebot11_installation` Bridge config fields, update the example, and validate one enabled pair with the expected surfaces
- [x] 4.2 Reject Bridge messages, notices, and commands with missing or mismatched `source_bot` and reject attempts to configure more than one installation pair
- [x] 4.3 Inject `BotOperationClient` plus the two installation refs into `BridgeForwardingRuntime` and remove its `BotRegistry` lookup/fallback logic
- [x] 4.4 Refactor Bridge handler, formatter, media, event, and command signatures so production code no longer passes `IBot&` or provider-interface references
- [x] 4.5 Migrate QQ-to-Telegram group/topic and Telegram-to-QQ group direct sends to typed results without changing existing message conversion
- [x] 4.6 Preserve pre-send dedupe, one direct mapping write after new delivery, no write for existing mapping, and mapping-failure behavior without a database migration
- [x] 4.7 Update isolated direct-forward tests and mapping-operation-count assertions to use a fake operation client and explicit installation pair

## 5. Migrate existing Bridge mutations, media, and commands

- [x] 5.1 Migrate current QQ/Telegram delete and Telegram text-edit paths to typed message references and mutation results
- [x] 5.2 Migrate OneBot group-member, forwarded-message, group/private-file, and poke calls to their provider-namespaced typed actions
- [x] 5.3 Replace Telegram media URL resolution/download calls with bounded `telegram.media.fetch_file` while preserving current caches, conversion, and cleanup
- [x] 5.4 Migrate Telegram cached-photo, URL media-group, and multipart media-group sends while preserving topics, replies, caption entities, fallbacks, and temporary-file cleanup
- [x] 5.5 Migrate current command/error replies through typed group send while preserving `recall`, heartbeat-based `checkalive`, `poke`, command completion, and propagation
- [x] 5.6 Add focused Bridge tests for media fetch/send bounds, upload fallback, edit/removal, provider lookups, commands, typed failure, and uncertain side effect

## 6. Migrate Bridge message retry without a generic outbox

- [x] 6.1 Change retry callbacks to submit exact-installation QQ group, Telegram group, and Telegram topic requests and return completed/definite/terminal/unknown typed dispositions
- [x] 6.2 Enqueue initial retries only for retryable `DefinitelyNotSubmitted` failures and report `PossiblySubmitted` without mapping or automatic resend
- [x] 6.3 Reschedule worker entries only for definite retryable failures and stop uncertain entries through the existing finite-attempt policy without changing retry tables
- [x] 6.4 Preserve successful retry mapping plus row removal, duplicate enqueue, bounded backoff, reload ownership, and shutdown lifetime behavior
- [x] 6.5 Add retry tests for all three target routes, provider rejection, unavailable route, malformed/uncertain send, exhaustion, completion persistence failure, reload, and shutdown

## 7. Migrate current `chat_llm` egress

- [x] 7.1 Change command parsing and runtime helpers to receive validated source platform/installation data instead of detecting bot type with RTTI
- [x] 7.2 Replace `chat_llm` `BotRegistry` resolution with `BotOperationClient` lookup by exact `source_bot` for messages and commands
- [x] 7.3 Migrate QQ/Telegram group replies and Telegram forum-topic replies/proactive sends to typed operations while preserving reply segments and conversation persistence
- [x] 7.4 Map typed errors to existing actor failures without provider JSON and preserve command completion, reload, and shutdown semantics
- [x] 7.5 Replace fake-bot fixtures with a fake operation client and cover exact route, missing source bot, group/topic behavior, failures, commands, proactive sends, reload, and shutdown

## 8. Guard the narrow boundary and validate

- [x] 8.1 Add architecture gates proving production Bridge and `chat_llm` no longer include, link, resolve, accept, or cast `BotRegistry`, bot/provider interfaces, concrete bots, or connection managers
- [x] 8.2 Verify compatibility `BotRegistry`, legacy bot interfaces, raw ingress, message-store, Bridge schemas, command-catalog publication, and unsupported platforms are otherwise unchanged
- [x] 8.3 Run root runtime-generation, validation-only, reload, in-flight operation, and shutdown tests with the shared operation client
- [x] 8.4 Update SDK and actor documentation with the closed action matrix, exact installation routing, Bridge config migration, retry-safety limitation, and explicitly deferred work
- [x] 8.5 Run root tests plus standalone installed-SDK suites for Bridge and `chat_llm` on fresh isolated builds
- [x] 8.6 Run `nix fmt` from the repository root and verify every repository under `local_actor/` is formatted before any commit
- [x] 8.7 Run `openspec validate refactor-capability-based-bot-api --strict --no-interactive` and confirm the artifacts and implementation contain no deferred ingress/outbox/blob/mapping-v2 work

## 9. Resolve runtime acceptance regressions

- [x] 9.1 Migrate the active development Bridge config and make required strings plus exact enabled bot-installation types fail during startup, validation-only, and reload contract validation
- [x] 9.2 Preserve HTTP transport submission phase so DNS/connect/proxy-tunnel/TLS-handshake failures are safely retryable while failures after request writing remain possibly submitted
- [x] 9.3 Distinguish expected unmapped/disabled/deferred Bridge no-ops from attempted delivery failures instead of returning `bridge_not_forwarded` for both
- [x] 9.4 Add transport-phase, actor config, Bridge no-op/failure, fresh installed-SDK, and conformance regression coverage
