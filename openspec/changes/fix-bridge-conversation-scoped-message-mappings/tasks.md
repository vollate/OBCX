## 1. Freeze the production regression and version-2 baseline

- [x] 1.1 Confirm `support-bridge-multi-installation-pairs` is the implementation prerequisite and capture the exact schema-version-2 mapping, retry, media-group, reload, and operation-count baseline
- [x] 1.2 Inventory every Bridge production mapping read, write, update, delete, reverse lookup, retry completion, media-group commit, and event payload that lacks source or target conversation identity
- [x] 1.3 Add an isolated fixture reproducing one Telegram installation with target message id `2700` in two chats mapped to two QQ groups, including the observed current and historical source ids
- [x] 1.4 Add failing regression tests showing the old reverse `LIMIT 1` path can construct a cross-group QQ reply and can reach destructive command/recall paths
- [x] 1.5 Add version-2 database fixtures for resolvable current routes, removed historical routes, absent Message Store history, multi-conversation source collisions, deferred albums, malformed rows, and pending retries
- [x] 1.6 Preserve direct-forward repository operation-count assertions for new delivery, already-persisted delivery, retry completion, and deferred media-group ownership before changing APIs

## 2. Introduce complete message identity and fail-closed resolution

- [x] 2.1 Add validated `BridgeMessageIdentity` and conversation helpers for QQ `group:<id>` and Telegram `chat:<id>` values, with topic id retained as separate route metadata
- [x] 2.2 Add typed mapping-resolution values for missing, unique, ambiguous, and corrupt outcomes with stable secret-safe diagnostic codes
- [x] 2.3 Replace repository reverse `LIMIT 1` access with candidate/exact-resolution primitives that never select by row order, timestamp, platform, installation, or native id alone
- [x] 2.4 Add a version-2 compatibility resolver that cross-checks candidates against exact Message Store bot/platform/conversation/message identity and the invocation's selected route
- [x] 2.5 Make a normal no-candidate reply retain existing safe missing-reply behavior while evidence of incomplete or ambiguous legacy candidates fails the whole affected operation
- [x] 2.6 Remove or make non-production every mapping API overload that omits source conversation, target conversation, or either exact installation
- [x] 2.7 Add focused tests proving the `2700` fixture resolves the current QQ group, never the historical group, and returns zero provider/mapping side effects when exact proof is unavailable
- [x] 2.8 Add source-scan gates rejecting unordered singular mapping lookup, first/newest/oldest-row selection, and platform/installation/message-id-only production APIs

## 3. Add migration-only route history and safety policy configuration

- [x] 3.1 Add immutable configuration values for migration-only legacy conversation routes and `legacy_unresolved_mapping_policy` with strict `fail` as the default
- [x] 3.2 Parse route-history entries as one exact configured pair, Telegram chat/optional topic, and QQ group without persisting pair aliases as message identity
- [x] 3.3 Validate canonical conversation syntax, configured pair membership, supported platform direction, duplicate source routes, conflicting histories, and unsupported policy values before database mutation
- [x] 3.4 Ensure scalar single-pair and named multi-pair configurations can both identify migration route history without bot fallback or credential-adjacent values
- [x] 3.5 Add configuration tests for valid current/history routes plus unknown pairs, duplicate histories, malformed conversations, wrong surfaces, mixed pair ownership, and invalid archive policy
- [x] 3.6 Update the tracked Bridge configuration example with commented strict/archive policy and historical-route examples clearly marked migration-only

## 4. Define schema version 3 and exact repository APIs

- [x] 4.1 Add source and target conversation ids plus explicit primary role to direct mapping and media-group storage models
- [x] 4.2 Add canonical source and target conversation ids to retry storage models and in-memory retry entries while retaining native group/topic request metadata
- [x] 4.3 Create empty-database version-3 mapping, retry, and media-group tables with non-null complete identities and no changes to user, sticker, QQ-sticker, or heartbeat tables
- [x] 4.4 Add exact forward and reverse indexes that permit equal native ids in different conversations and do not impose global target-message uniqueness
- [x] 4.5 Replace incomplete `INSERT OR REPLACE` behavior with an explicit exact-key upsert that cannot replace another conversation's row
- [x] 4.6 Implement exact mapping add/resolve/update/delete APIs using complete identities and typed ambiguity/corruption outcomes
- [x] 4.7 Implement exact retry enqueue/update/remove/restore APIs whose uniqueness includes source and target conversations
- [x] 4.8 Implement exact media-group add/read APIs with source/target conversations and one persisted semantic primary source
- [x] 4.9 Add repository tests proving same-installation source and target id collisions coexist and remain isolated for all reads, writes, updates, deletes, retries, and media-group operations

## 5. Implement transactional version-2 to version-3 migration

- [x] 5.1 Add schema inspection that recognizes empty, version 1, version 2, version 3, malformed, and newer Bridge state without modifying it
- [x] 5.2 Build transaction-local Message Store source-identity lookup structures without creating or changing persistent Message Store tables or indexes
- [x] 5.3 Classify version-2 direct mappings from exact Message Store source identity plus current route or validated explicit route history
- [x] 5.4 Classify version-2 Telegram topic mappings by stored payload/thread evidence and require route history when the target QQ group cannot be proven
- [x] 5.5 Derive one deferred-album primary from captured Message Store media-group order/caption ownership and classify missing or multiple primary evidence as unresolved
- [x] 5.6 Migrate pending retry rows only when both conversations can be derived from persisted source/target group metadata; reject archive, fallback, or retargeting for unresolved retries
- [x] 5.7 Implement strict-policy rollback with bounded per-reason diagnostics when any mapping, media row, or retry is unresolved, ambiguous, malformed, or colliding under its exact key
- [x] 5.8 Implement explicit archive-policy copying of unresolved mapping/media rows into a namespaced non-live legacy table that no production repository API exposes
- [x] 5.9 Transactionally rebuild tables, copy resolvable rows, create indexes, verify primary ownership and live-plus-archived counts, and publish version 3 only after all checks pass
- [x] 5.10 Make version-3 reopen idempotent, reject newer versions, preserve version-1 prerequisite behavior, and reject version-2 migration during reload as restart-required
- [x] 5.11 Add migration tests for empty creation, both known `2700` rows, current routes, route history, strict failure, explicit archive, missing source history, ambiguous source history, albums, pending retries, rollback, newer version, and repeated open
- [x] 5.12 Add a production-scale isolated fixture near the observed row volume and assert bounded migration time, exact counts, no Message Store schema diff, and no cross-conversation collapse

## 6. Carry conversations through direct forwarding and actor persistence

- [x] 6.1 Add source and target conversation ids to `DirectForwardOutcome` and `BridgeForwardResult` and require handlers to populate them from the selected immutable route
- [x] 6.2 Update QQ-to-Telegram direct, topic, inline media-group, already-persisted, failed, and retry-enqueued outcomes to retain complete source and target scopes
- [x] 6.3 Update Telegram-to-QQ direct, topic, edited-resend, already-persisted, failed, and deferred outcomes to retain complete source and target scopes
- [x] 6.4 Update `BridgeActor` mapping construction and validation to reject any delivered result missing either complete identity before the single direct write
- [x] 6.5 Add both conversation ids to `MessageForwarded` payloads and include source conversation in emitted event identity without changing the event type
- [x] 6.6 Scope pre-send de-duplication to complete source and selected target conversations while preserving one pre-send read, no post-send recovery read, and at most one actor-owned write
- [x] 6.7 Add same-installation/two-chat direct-forward tests proving exact target calls, independent rows, complete events, and unchanged per-delivery operation counts

## 7. Scope replies, edits, recalls, and commands

- [x] 7.1 Centralize exact forward/reverse mapping resolution for handlers using current route conversations and existing exact Message Store lookups
- [x] 7.2 Update normal Telegram-to-QQ reply formatting to resolve direct and reverse mappings in the current Telegram chat and paired QQ group
- [x] 7.3 Update deferred Telegram media-group reply formatting to use the exact target conversation and persisted semantic primary source
- [x] 7.4 Inject exact Message Store resolution into QQ reply formatting and scope both direct and reverse paths to the current QQ group and paired Telegram chat
- [x] 7.5 Update Telegram `/recall` and `/poke` to reject ambiguous or route-inconsistent mappings before delete/poke dispatch and to mutate only exact mapping rows
- [x] 7.6 Update Telegram edit/replacement handling to resolve, delete, resend, update, or remove only the complete source/target mapping
- [x] 7.7 Update QQ recall notices to use the notice's exact QQ group and persisted Telegram chat, rejecting route disagreement before edit/delete dispatch
- [x] 7.8 Replace the generated recall tab with a space and add generated-text C0 sanitization before MarkdownV2 escaping without changing incoming message conversion
- [x] 7.9 Add tests reproducing the observed `2702` reply-to-`2700` flow and proving the emitted QQ reply references the current group's source id rather than the historical group's id
- [x] 7.10 Add collision tests for QQ replies, Telegram replies, edits, QQ recall, `/recall`, and `/poke`, asserting typed ambiguity plus zero provider/mapping side effects where proof is incomplete
- [x] 7.11 Add recall-text tests for tab, carriage return, NUL, MarkdownV2 punctuation, picture deletion, sender-visible, and sender-hidden paths

## 8. Make retries conversation scoped

- [x] 8.1 Carry both canonical conversations through retry creation, serialization conversion, durable identity, in-memory duplicate identity, and secret-safe logs
- [x] 8.2 Construct retry requests from the persisted exact target installation/conversation and topic metadata rather than current config or callback defaults
- [x] 8.3 Scope durable enqueue, update, remove, ready-row restore, pre-send mapping check, success mapping write, and cleanup by both complete identities
- [x] 8.4 Validate typed retry success against the persisted target installation and conversation before accepting its native target message id
- [x] 8.5 Treat removed/mismatched target conversations as exact unavailable-route failures and never resend through another group, chat, installation, or pair
- [x] 8.6 Preserve `DefinitelyNotSubmitted` rescheduling, `PossiblySubmitted` terminalization, finite attempts, and one generation-owned worker independently for every conversation
- [x] 8.7 Add concurrent collision tests for duplicate enqueue, restore, success, existing exact mapping, different-conversation mapping, exhaustion, unavailable route, uncertain outcome, and completion persistence failure
- [x] 8.8 Re-run reload and shutdown tests with pending/in-flight retries from several conversations and verify version-2 unresolved retries block migration without provider calls

## 9. Make deferred media-group persistence deterministic

- [x] 9.1 Carry source and target conversations through Telegram album buffer flush captures, direct media processing, and deferred persistence
- [x] 9.2 Mark exactly the first semantic album item primary and persist all other source mappings as non-primary against the same exact target identity
- [x] 9.3 Scope media-group de-duplication, repair writes, reverse reply resolution, cancellation, and shutdown draining to installation/chat/media-group identity
- [x] 9.4 Add same-installation colliding chat/media-group/message-id tests proving independent debounce, target dispatch, primary selection, and reverse resolution
- [x] 9.5 Add corruption tests for zero/multiple primaries and verify fail-closed diagnostics with no arbitrary row selection or provider side effect

## 10. Document operations and enforce architecture boundaries

- [x] 10.1 Document complete message identity, why Telegram ids are chat-scoped, the observed collision shape, and why duplicate native ids must not be deleted
- [x] 10.2 Document migration-only route history, strict/archive policy, unresolved reason handling, pending-retry requirements, and non-live archive limitations
- [x] 10.3 Document stop/SQLite-consistent backup/startup-preflight/version-3 verification and binary-plus-database rollback procedures, including WAL safety
- [x] 10.4 Document that already-sent incorrect reply content is not automatically reconciled and must be explicitly removed/resubmitted after repair
- [x] 10.5 Add source-scan gates rejecting provider message lookup, unsafe mapping fallback, pair-order selection, global target-id uniqueness, Message Store DDL, and production reads from the legacy archive
- [x] 10.6 Confirm raw ingress, Message Store schema/events, exact-installation pair routing, 13-action matrix, transports, media conversion, no-op semantics, and deferred non-goals remain unchanged

## 11. End-to-end validation

- [x] 11.1 Run strict OpenSpec validation and verify this change's deltas compose after `support-bridge-multi-installation-pairs` without reverting installation-scoped requirements
- [x] 11.2 Run root actor-contract, runtime-generation, validation-only, reload, installed-SDK, architecture, scope, and complete test tiers with full build parallelism
- [x] 11.3 Build and test Message Store unchanged against a fresh installed SDK and verify no source, schema, index, or event-contract modification
- [x] 11.4 Build and run the complete standalone Bridge suite against the same SDK, including the exact `2700` business regression, all migration policies, operation counts, installed pipeline, reload, and shutdown
- [x] 11.5 Build and run `chat_llm` unchanged against the same fresh SDK to catch actor-contract or package regressions
- [x] 11.6 Validate scalar and named-pair deployment configs plus valid/invalid route-history and archive-policy configurations with rebuilt validation/startup paths
- [x] 11.7 Run `nix fmt` from the repository root and `git diff --check` in root, Bridge, Message Store, and `chat_llm`
- [x] 11.8 Audit an isolated copy of production-shaped state to verify version 3, live-plus-archived counts, both conversation-scoped `2700` rows, zero pending ambiguity, and unchanged Message Store schema
- [x] 11.9 Confirm implementation contains no new provider action, typed ingress, Message Store migration, fan-out, outbox, reconciliation, blob, audit-stream, or rate-governor work

## 12. Repair startup migration activation regression

- [x] 12.1 Reproduce the live failure from logs and read-only schema inspection, proving strict migration rolled back intact at version 2 before later events issued version-3 retry SQL
- [x] 12.2 Correct migration route selection so Telegram forum-thread metadata uses an exact topic route when present but remains valid under a chat-wide group-to-group route
- [x] 12.3 Publish a Bridge repository only after schema initialization commits so failed migration candidates cannot poison later event handling
- [x] 12.4 Add an ABI-compatible typed generation-preparation hook and invoke it before scheduler, command, or pipeline route activation while treating older V2 actors as ready
- [x] 12.5 Prepare Bridge schema on startup, perform configuration-only validation in validation mode, and return typed restart-required from read-only version checks during reload
- [x] 12.6 Add regressions for the three observed forum-thread rows, missing thread evidence, repeated events after migration rollback, typed actor preparation, and pre-ingress installed startup preparation
- [x] 12.7 Run schema v3 migration against a SQLite-consistent isolated production snapshot and verify integrity, all 90,102 mappings, all 329 retries, no archives, both `2700` rows, and all three thread rows
- [x] 12.8 Re-run formatting, strict OpenSpec validation, source gates, full root/Bridge/Message Store/chat_llm suites, and installed SDK pipeline/reload validation after the repair
