## Context

`support-bridge-multi-installation-pairs` makes installation identity part of Bridge schema version 2, but deliberately retains the older mapping key shape inside one installation. A Bridge message mapping is currently identified by source installation/platform/message id and target installation/platform; neither source nor target conversation is stored. Telegram guarantees `message_id` only within a chat, and OneBot message references must also be interpreted in their originating group.

Production exposed the consequence immediately. Telegram target id `2700` validly existed in two chats on the same Telegram installation. Reverse lookup queried installation/platform/id and used unordered `LIMIT 1`, so a reply in one Telegram chat selected a QQ source message from another QQ group. A destructive command can follow the same path. The two rows are not duplicates and neither may be deleted merely because their native target ids are equal.

The change is a prerequisite-sensitive follow-up to `support-bridge-multi-installation-pairs`: that change and its version-2 installation migration must be finalized first. Raw ingress and Message Store already carry `source_bot` and `conversation_id`; only Bridge-owned state and resolution are incomplete. The existing 13 bot actions are sufficient, so provider-side message lookup is neither necessary nor permitted.

## Goals / Non-Goals

**Goals:**

- Make every production Bridge message lookup use complete source and target message identities: installation, platform, conversation, and native message id.
- Stop unsafe reverse lookup before any provider side effect; ambiguity must be observable and fail closed.
- Advance Bridge-owned message state to schema version 3 without collapsing equal ids from different conversations.
- Preserve current direct-forward single-owner mapping writes and no-post-send-recovery-read counts.
- Migrate resolvable version-2 state deterministically, preserve explicitly archived unresolved history without making it queryable by production mutation paths, and reject all other ambiguity.
- Keep retry completion, deferred media groups, replies, edits, recalls, and commands consistent with the same exact identity.
- Correct Bridge-generated QQ recall replacement text so control characters cannot fail local operation validation.

**Non-Goals:**

- Changing raw ingress, `common::MessageEvent`, Message Store tables/events, or introducing a universal message model.
- Adding `get_msg`, another bot action/provider, fan-out, an outbox, outcome reconciliation, a blob gateway, or rate governance.
- Guessing a historical conversation from message-id magnitude, timestamps, insertion order, current pair order, or provider-specific heuristics.
- Automatically repairing the content of a message that was already sent with an incorrect reply reference; operators may remove and resend that message after deployment.
- Making Telegram topic id part of Telegram message uniqueness. Telegram message ids are chat-scoped; topic id remains separate route metadata.

## Decisions

### 1. Use a complete Bridge message identity

Introduce a value type equivalent to:

```text
BridgeMessageIdentity {
  installation_id
  platform
  conversation_id
  message_id
}
```

A `MessageMapping` contains one complete source identity and one complete target identity. QQ group conversations use the existing Message Store form `group:<native-group-id>` and Telegram chats use `chat:<native-chat-id>`. Construction and parsing use shared validated helpers rather than ad-hoc string concatenation. Telegram topic id remains optional route metadata because message ids are unique across the containing chat.

For the observed collision, schema version 3 retains both mappings:

```text
qq_bot/qq/group:1012634788/1162814500
  -> telegram_bot/telegram/chat:-1003150190260/2700

qq_bot/qq/group:823971580/-1583402916
  -> telegram_bot/telegram/chat:-5281838703/2700
```

Pair aliases are not persisted. Installations and conversations are stable operation identities; renaming a pair must not make state unreadable.

**Rejected alternatives:** installation scoping alone still collides inside one bot; adding only source group cannot safely perform reverse mutation; treating Telegram ids as bot-global contradicts the provider contract; including topic in message uniqueness creates two identities for a chat-global id without solving another collision.

### 2. Replace optional singular lookup with typed exact resolution

Repository APIs accept complete identities or exact source/target scopes. Forward resolution returns `Missing`, `Unique(mapping)`, or `Ambiguous`; reverse resolution returns the same typed result and never hides multiple rows behind `LIMIT 1`. Production code has no platform-, installation-, or message-id-only overload.

A normal missing reply mapping retains current behavior where safe, such as removing an unresolvable reply segment before forwarding. Evidence of legacy candidates that cannot be assigned to the invocation's exact conversations is different: it returns `ambiguous_message_mapping`, emits a secret-safe diagnostic, and performs no send, edit, delete, poke, or mapping mutation. Commands return a bounded user-facing failure. No alternate pair, current-first, newest-row, or provider lookup is attempted.

All reverse paths use both sides supplied by the selected route:

- Telegram reply/`recall`/`poke`: current Telegram chat and paired QQ group;
- QQ reply and recall notice: current QQ group and paired Telegram chat;
- edit/replacement: original source chat/group and selected target group/chat;
- deferred media group: buffered source chat and persisted target group.

A source-target mapping for a deferred album may legitimately have several source message ids and one target message id. Version 3 persists one explicit `primary` role from album order/caption ownership. Reverse resolution uses that role; missing or multiple primaries are corruption, not an invitation to select an arbitrary row.

**Rejected alternatives:** `ORDER BY created_at DESC LIMIT 1` merely changes which group is wrong; deleting duplicate target ids destroys valid mappings; forwarding without the ambiguous reply may be safe for some messages but is not safe for `/recall` or for preserving exact user intent, so ambiguity blocks the whole affected operation.

### 3. Add schema version 3 only to Bridge-owned message state

`bridge_message_mappings` gains `source_conversation_id`, `target_conversation_id`, and an explicit primary role. Its exact forward key includes source installation/platform/conversation/message plus the selected target installation/platform/conversation. A reverse index begins with target installation/platform/conversation/message and includes source scope. Indexes support equal native ids in different conversations and deterministic primary resolution without making all target ids globally unique.

`bridge_message_retry_queue` gains canonical source and target conversation ids. Its durable uniqueness includes both conversations, and successful completion writes the same values into the mapping before deleting the exact retry row. Existing source/target group fields become request metadata or are replaced by validated conversion helpers; they are not a competing identity.

`bridge_media_group_mappings` gains source and target conversation ids plus primary information sufficient to create deterministic mapping rows. Existing installation-scoped user, sticker, QQ-sticker, and heartbeat tables do not change in version 3.

Direct sends still perform one exact pre-send mapping lookup and at most one actor-owned direct mapping write. Retry and deferred media groups keep their specialized owners. `INSERT OR REPLACE` against an incomplete key is replaced by an explicit exact-key upsert whose conflict target cannot replace a row from another conversation.

**Rejected alternatives:** changing the Message Store schema duplicates already-available identity; introducing a generic cross-actor message table expands scope; nullable conversation columns in the production mapping table leave an unsafe fallback permanently available.

### 4. Classify version-2 rows before publishing version 3

Initialization runs under the existing migration lock and one SQLite transaction. It builds an immutable migration plan before dropping or renaming any version-2 table:

1. Match each mapping's source installation/platform/message id against existing Message Store rows.
2. Require exactly one source conversation, unless an explicit migration route disambiguates a known collision.
3. Resolve the target conversation from the exact installation pair and current Bridge route. Stored Telegram thread metadata selects an exact topic-to-group route when one exists; a chat-wide group-to-group route deliberately applies to every forum thread in its containing chat and does not become unresolved merely because the stored message has `message_thread_id` metadata.
4. If the current route no longer exists, consult migration-only `legacy_mapping_routes` entries that identify a pair and exact Telegram chat/optional topic plus QQ group.
5. Classify the row as resolvable, ambiguous, or unresolved with a bounded reason code.

Message Store is read as-is. Migration may create a transaction-local lookup table/index, but it MUST NOT alter Message Store's persistent schema. Route-history entries are non-secret, validated for pair membership and uniqueness before the transaction, and ignored after schema version 3 is committed.

Default `legacy_unresolved_mapping_policy = "fail"` aborts on any unresolved or ambiguous mapping. An operator may explicitly select `"archive"`; then unresolved version-2 mapping/media rows are copied to a namespaced read-only legacy table with their original fields and bounded reason code. No production lookup, de-duplication, retry, command, or mutation API can read that table. Resolvable version-3 rows plus explicitly archived rows must equal the version-2 count. Active retry rows cannot be archived because they represent pending side effects; every retry must migrate exactly or migration fails until the operator drains or explicitly removes it after backup.

For an album with several source rows, primary ownership is derived from stored Message Store media-group order and Telegram caption/reply ownership. If that evidence is absent, the set is unresolved rather than selecting minimum row id or timestamp.

**Rejected alternatives:** assigning current routes to every old row silently rewrites history when mappings changed; dropping unresolved rows loses operator state; retaining nullable legacy rows in the live table reintroduces fallback; silently archiving pending retries can lose intended sends.

### 5. Carry conversations through forwarding rather than reconstructing them

`DirectForwardOutcome`, `BridgeForwardResult`, and actor-owned `MessageMapping` construction carry source and target conversation ids selected before dispatch. A successful typed send supplies only the native target id; the handler combines it with the already-selected exact target installation and conversation. The actor validates all identity fields before the one direct write.

`MessageForwarded` adds `source_conversation_id` and `target_conversation_id` while preserving its existing event type and fields. Its event id includes source installation, conversation, and native message id to prevent cross-chat collisions. No post-send repository read is added.

Mutation code uses the target conversation persisted in the mapping and verifies it agrees with the invocation's selected pair/route before constructing a typed `BotMessageRef`. A disagreement is a route/state error and causes no provider call.

**Rejected alternatives:** reconstructing conversation after send depends on mutable config; storing pair alias instead of conversations breaks harmless alias renames; adding a post-send mapping read violates the existing operation-count contract.

### 6. Reuse complete identity in retries and deferred media groups

`MessageRetryEntry`, serialization, in-memory duplicate identity, persistence CRUD, restore, pre-send check, success mapping, and cleanup all include source and target conversations. Callback dispatch still uses exact target installation and persisted native target group/topic request metadata. Changing configuration cannot retarget a durable retry.

Telegram album buffer keys remain installation/chat/media-group scoped. Deferred mapping writes contain both conversations and mark the Telegram album's first semantic source as primary. Equal album and message ids in another chat or pair remain independent.

**Rejected alternatives:** deriving conversations from callback registration or current config after restart permits retargeting; leaving retry uniqueness conversation-free can suppress a legitimate retry in another chat.

### 7. Sanitize only Bridge-generated recall replacement text

The hard-coded tab between sender label and recall marker becomes a normal space. Before MarkdownV2 escaping and typed edit dispatch, Bridge-generated replacement text replaces disallowed C0 controls with spaces while retaining supported line feeds. User message/media conversion behavior is unchanged. Tests cover sender display names containing tabs, carriage returns, and NUL bytes.

**Rejected alternatives:** weakening core operation validation would allow malformed text from every caller; removing all formatting changes visible behavior unnecessarily.

### 8. Prepare actor-owned schema before generation activation

A version-2-to-version-3 migration is startup-only and must finish before any message, notice, command, retry worker, or media buffer can use Bridge state. The V2 export helper therefore exposes an additive optional generation-preparation entry point. `RuntimeGenerationBuilder` invokes it after the actor's validated configuration and generation services are registered but before the actor is registered with the scheduler/orchestrator. Existing V2 DSOs without the optional symbol and reflected actors without `prepare_generation` remain ready with unchanged behavior.

Bridge's typed hook parses its actor-specific configuration in every generation. Validation-only preparation stops there and performs no database operation. Startup preparation initializes or migrates the repository transactionally. Reload preparation performs read-only version-3 shape validation; version 1/2 returns typed restart-required before taking a migration lock or modifying state. A failed candidate is destroyed without replacing the active generation.

A repository candidate is published into `BridgeActor` only after initialization commits. If migration rolls back, the candidate is discarded; later calls cannot bypass initialization and issue version-3 SQL against intact version-2 tables. Version 3 reopen is idempotent. Diagnostics report schema versions, per-table bounded counts, route-history/archive decisions, and reason counts without message bodies, signed URLs, credentials, or complete provider responses.

The version-2 binary cannot safely read version 3. Rollback always restores the stopped-process database snapshot together with the old binary; no down-migration is provided.

## Risks / Trade-offs

- **[Historical routes may no longer be configured]** → Default to migration failure, provide explicit validated route-history entries, and require a conscious archive choice for inactive mappings.
- **[Archiving unresolved mappings makes old replies unavailable]** → Make archive opt-in, report exact bounded counts, keep rows inspectable but inaccessible to production mutation paths, and document that restoring route history before migration preserves them.
- **[Message Store lacks a source row for very old mappings]** → Classify the row unresolved; never infer conversation from id range or timestamp.
- **[Same target belongs to a deferred album]** → Persist and verify one semantic primary rather than imposing a globally unique target id.
- **[Migration scans a large shared SQLite database]** → Build transaction-local lookup structures, use set-based copies, log timings/counts, hold the existing migration lock, prepare before ingress publication, and benchmark with a production-scale isolated fixture.
- **[A fail-closed reply reduces availability]** → Prefer dropping one forwarding operation over quoting or deleting another conversation's message; provide an actionable diagnostic.
- **[A target was already sent with the wrong reply]** → Do not mutate it automatically; operator removal/resend is explicit because outcome reconciliation is out of scope.
- **[Version 3 blocks binary-only rollback]** → Require a SQLite-consistent pre-migration snapshot and document binary-plus-database restoration.

## Migration Plan

1. Finalize and deploy `support-bridge-multi-installation-pairs`; verify the database reports schema version 2.
2. Add collision regression tests and first replace unsafe reverse `LIMIT 1` behavior with fail-closed exact-conversation resolution so a stopgap build can be deployed without changing the database.
3. Add complete identity values, repository APIs, forwarding outcomes, retry/media propagation, and schema-version-3 fixtures.
4. Implement migration planning, route-history validation, strict/archive policy, transactional copy verification, reload gate, and idempotent reopen.
5. Run isolated production-scale migration tests and all root, Bridge, Message Store, and `chat_llm` installed-SDK suites.
6. Stop OBCX and take a SQLite-consistent snapshot using SQLite backup while quiesced; do not copy only the main file while WAL writes are active.
7. Start the candidate with default strict policy. If preflight reports unresolved inactive routes, stop, add explicit route history or consciously select archive after inspection, restore the snapshot if needed, and retry.
8. Verify schema version 3, per-table migrated/archive counts, both known `2700` mappings in separate conversations, zero pending migration ambiguity, and normal bidirectional forwarding.
9. Remove or recall the already-sent incorrect reply and resend it manually if desired; do not delete either valid historical `2700` mapping.
10. Roll back only by stopping OBCX and restoring both the previous binary and pre-version-3 database snapshot.

## Open Questions

None. Safety defaults are strict: unresolved history blocks migration unless the operator explicitly supplies route history or chooses non-queryable archival, and pending retries are never silently archived.
