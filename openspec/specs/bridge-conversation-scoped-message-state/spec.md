# bridge-conversation-scoped-message-state Specification

## Purpose
Define complete Bridge conversation identity, safe resolution, and transactional schema-3 migration.

## Requirements
### Requirement: Bridge mappings use complete conversation-scoped message identities
Every live Bridge message mapping SHALL identify its source and target by exact installation, platform, canonical conversation id, and native message id. QQ group conversations MUST use the canonical `group:<id>` identity, Telegram chat conversations MUST use `chat:<id>`, and Telegram topic id MUST remain separate route metadata rather than replacing the containing chat identity. Pair aliases MUST NOT be persisted as message identity.

#### Scenario: A direct QQ message is mapped to Telegram
- **WHEN** a QQ message is delivered through one selected route
- **THEN** the mapping records the exact OneBot installation, QQ group conversation, QQ message id, Telegram installation, Telegram chat conversation, and Telegram message id

#### Scenario: Equal Telegram ids exist in different chats
- **WHEN** the same Telegram installation has target message id `2700` in two configured chats
- **THEN** mappings for both chats coexist and each complete target identity resolves only its corresponding source conversation

#### Scenario: Equal source ids exist in different conversations
- **WHEN** one installation observes the same native source message id in two conversations
- **THEN** both source identities remain independent for de-duplication, update, delete, and reverse resolution

#### Scenario: A mapping identity is incomplete
- **WHEN** a live mapping write omits either conversation or supplies a conversation incompatible with its platform
- **THEN** the repository rejects it without creating or replacing a row

#### Scenario: A Telegram topic message is mapped
- **WHEN** a Telegram topic route forwards a message
- **THEN** the source message identity uses its containing Telegram chat while topic id remains available as route metadata for target selection

### Requirement: Forward and reverse resolution fail closed on ambiguity
Production mapping APIs SHALL require complete invocation and expected-route scopes. Resolution SHALL distinguish missing, unique, ambiguous, and corrupt state; it MUST NOT select by platform, installation, pair order, timestamp, row order, or unordered `LIMIT 1`. An ambiguous or corrupt resolution MUST produce a typed secret-safe diagnostic and MUST cause no provider send, edit, delete, poke, or mapping mutation.

#### Scenario: Production collision is resolved by conversation
- **WHEN** Telegram chat A and chat B both contain target message id `2700` and an invocation in chat A expects QQ group A
- **THEN** reverse resolution returns only the group-A source mapping and never the group-B message

#### Scenario: A mapping does not exist
- **WHEN** no current or legacy candidate exists for an optional reply reference
- **THEN** Bridge may retain its existing safe missing-reply behavior without fabricating a mapping

#### Scenario: Legacy candidates remain ambiguous
- **WHEN** one or more legacy candidates exist but exact source and target conversations cannot identify one valid mapping
- **THEN** Bridge reports `ambiguous_message_mapping` and performs no provider or mapping side effect

#### Scenario: A destructive command is ambiguous
- **WHEN** `/recall`, recall propagation, edit replacement, or `/poke` cannot resolve one complete mapping in the invocation's route
- **THEN** Bridge rejects the operation before dispatch and does not try another conversation or installation

#### Scenario: Archived legacy state exists
- **WHEN** a version-2 row was explicitly archived during migration
- **THEN** no production de-duplication, reply, mutation, retry, or command API can resolve that row

### Requirement: Fan-in mappings have an explicit primary source
When a deferred media group maps several source message identities to one target message identity, Bridge SHALL persist exactly one semantic primary source for reverse reply resolution. The primary MUST derive from the captured album order and caption/reply ownership; missing or multiple primaries MUST be treated as corrupt state rather than resolved by row id or timestamp.

#### Scenario: A Telegram album is forwarded to one QQ message
- **WHEN** several Telegram album items are persisted against one QQ target
- **THEN** all exact source mappings are retained and exactly the semantic first item is marked primary

#### Scenario: A reply targets a forwarded album
- **WHEN** reverse resolution starts from the album's complete QQ target identity
- **THEN** it returns the persisted primary Telegram source in the same chat

#### Scenario: Migrated album primary cannot be proven
- **WHEN** version-2 album rows do not contain enough Message Store evidence to identify one semantic primary
- **THEN** migration classifies the set unresolved instead of choosing minimum id, row order, or timestamp

### Requirement: Bridge schema version 3 migrates message state transactionally
Bridge SHALL record schema version 3 for conversation-scoped mappings, retries, and media-group mappings. Version-2 inspection, migration planning, table rebuilds, copies, indexes, count checks, optional unresolved archives, and version publication MUST execute under the database migration lock and one SQLite transaction. Version 3 MUST preserve all resolvable rows without cross-conversation replacement.

#### Scenario: A new database starts
- **WHEN** Bridge opens an empty actor namespace
- **THEN** it creates complete version-3 tables, indexes, and the version record directly

#### Scenario: A resolvable version-2 mapping is migrated
- **WHEN** exact Message Store source identity and a validated current or historical Bridge route determine both conversations
- **THEN** migration copies the row with complete source and target identities

#### Scenario: Equal target ids are migrated
- **WHEN** two version-2 rows use the same target installation/platform/message id but route to different target conversations
- **THEN** version 3 retains both rows and count verification treats them as distinct

#### Scenario: Current routing no longer contains a historical route
- **WHEN** an operator supplies a unique validated migration-only route-history entry for that version-2 source conversation
- **THEN** migration uses the explicit historical target conversation and ignores the entry after version 3 is published

#### Scenario: Group-to-group source message carries forum thread metadata
- **WHEN** Message Store identifies a Telegram source message in a forum thread and the exact configured route is chat-wide group-to-group
- **THEN** migration uses the containing Telegram chat and group-to-group QQ target without requiring a topic route or reporting `route_history_missing`

#### Scenario: Topic route lacks exact thread evidence
- **WHEN** only topic-to-group routes exist for a Telegram chat and Message Store does not identify the source message's exact thread
- **THEN** migration leaves the row unresolved rather than choosing the sole or first configured topic

#### Scenario: Strict migration has unresolved rows
- **WHEN** any mapping or media row cannot be assigned complete identity and the unresolved policy is `fail`
- **THEN** migration aborts with bounded reason counts and leaves every version-2 table unchanged

#### Scenario: Operator explicitly archives unresolved mappings
- **WHEN** the unresolved policy is explicitly `archive`
- **THEN** unresolved mapping/media rows are copied to a namespaced non-live legacy table, live plus archived counts equal version 2, and production repository APIs cannot read the archive

#### Scenario: A pending retry is unresolved
- **WHEN** a version-2 retry lacks an exact source or target conversation
- **THEN** migration fails until the operator safely drains or explicitly removes it after backup and MUST NOT silently archive or retarget the pending side effect

#### Scenario: Migration copy verification fails
- **WHEN** table shape, supported direction, primary ownership, uniqueness, or migrated-plus-archived count verification fails
- **THEN** the transaction rolls back and schema version 3 is not published

#### Scenario: Version 3 is opened again
- **WHEN** Bridge initializes an already valid version-3 database
- **THEN** initialization is idempotent and does not recopy, archive, or rewrite mappings

#### Scenario: A newer schema is encountered
- **WHEN** the database records a Bridge schema version newer than 3
- **THEN** activation fails without modifying Bridge state

### Requirement: Migration planning uses existing Message Store identity without changing it
Version-2 mapping classification MAY read current Message Store rows by source platform, source bot, conversation id, and message id and MAY create transaction-local lookup structures. It MUST NOT alter Message Store tables, indexes, events, payloads, or ownership. Missing or multiply matching source identity MUST remain unresolved unless explicit route history proves one complete mapping.

#### Scenario: Source identity exists once
- **WHEN** Message Store contains exactly one matching source bot/platform/message in one conversation
- **THEN** migration may use that canonical source conversation for route resolution

#### Scenario: Source message id occurs in several conversations
- **WHEN** Message Store contains the same installation/platform/message id in more than one conversation
- **THEN** migration does not choose one without additional exact validated evidence

#### Scenario: Source history is absent
- **WHEN** no Message Store row identifies a version-2 mapping's source conversation
- **THEN** the row is unresolved and no timestamp or id-range heuristic is used

#### Scenario: Message Store schema is inspected after migration
- **WHEN** version 3 migration completes
- **THEN** Message Store schema and event contracts are byte-for-byte unchanged by Bridge migration

### Requirement: Schema migration is restart-gated and observable
A version-2-to-version-3 migration SHALL run only when no previous Bridge generation can access the database. A reload candidate that requires migration MUST return a restart-required failure before modification. Diagnostics SHALL report schema versions, per-table bounded counts, route-history/archive policy, ambiguity reason counts, and duration without message bodies, credentials, signed URLs, or complete provider responses.

#### Scenario: Startup migrates version 2
- **WHEN** normal startup has no active Bridge generation and the migration plan satisfies the selected policy
- **THEN** version 3 becomes visible before ingress, retry workers, or media buffers start

#### Scenario: Reload candidate sees version 2
- **WHEN** an active generation exists and a candidate would require version-3 migration
- **THEN** reload fails as restart-required and leaves the active generation and database unchanged

#### Scenario: Migration reports a collision count
- **WHEN** preflight finds equal native ids in different conversations
- **THEN** diagnostics report bounded aggregate counts without treating valid cross-conversation rows as corruption

#### Scenario: Rollback is required
- **WHEN** an operator returns to a version-2 binary after version 3 was published
- **THEN** the documented procedure requires restoring both that binary and the pre-migration SQLite snapshot rather than running a down-migration

### Requirement: Conversation scoping introduces no unrelated subsystem
Version 3 SHALL remain inside the Bridge actor namespace and existing QQ/Telegram operation boundary. It MUST NOT add a provider message-lookup action, generic outbox, reconciliation journal, blob store, audit event stream, typed ingress model, Message Store migration, fan-out route, or unsupported platform.

#### Scenario: Operation matrix is inspected
- **WHEN** the change is complete
- **THEN** the existing 13 QQ/Telegram bot actions are unchanged

#### Scenario: Version-3 tables are inspected
- **WHEN** Bridge-owned SQLite schema is audited
- **THEN** it contains only complete message identity, retry/media metadata, schema versioning, and an explicitly selected non-live legacy archive needed for this repair
