## ADDED Requirements

### Requirement: Bridge state has an explicit installation-scoped schema version
Bridge SHALL record an actor-namespace schema version for its owned tables. The current unversioned Bridge tables SHALL be treated as version 1, and the installation-scoped tables SHALL be version 2. Schema inspection, creation, and migration MUST execute under the existing database migration lock and in one transaction.

#### Scenario: A new database starts
- **WHEN** Bridge opens an empty actor namespace
- **THEN** it creates the version-2 tables, indexes, and version record without requiring legacy-pair configuration

#### Scenario: Version 2 is opened again
- **WHEN** Bridge initializes a database whose recorded schema version is 2
- **THEN** initialization is idempotent and does not copy, drop, or rewrite existing rows

#### Scenario: A newer schema is encountered
- **WHEN** the database records a Bridge schema version newer than the binary supports
- **THEN** activation fails without modifying any Bridge table

### Requirement: Legacy single-pair rows migrate only through a deterministic pair
A non-empty version-1 Bridge database SHALL be migrated using exactly one legacy Telegram/OneBot pair. The pair SHALL be the scalar compatibility pair, the sole named pair, or the named `legacy_state_pair`. When several named pairs exist and no valid selector is supplied, Bridge MUST fail migration rather than assigning rows to a default or first pair.

#### Scenario: Scalar single-pair state is migrated
- **WHEN** version-1 rows are opened with the existing scalar Telegram and OneBot installation fields
- **THEN** Telegram and QQ row directions are assigned to those exact installations

#### Scenario: One named pair is configured
- **WHEN** version-1 rows are opened with exactly one valid named pair
- **THEN** that pair is used without requiring a separate legacy selector

#### Scenario: Several pairs need legacy ownership
- **WHEN** a non-empty version-1 database is opened with multiple named pairs and `legacy_state_pair` identifies one of them
- **THEN** all valid legacy row directions are assigned to that pair and no other pair

#### Scenario: Legacy ownership is ambiguous
- **WHEN** a non-empty version-1 database is opened with multiple named pairs and no valid legacy selector
- **THEN** migration fails with an actionable secret-safe diagnostic and commits no schema or row change

### Requirement: Migration is complete, checked, and atomic
Bridge SHALL rebuild every affected table with version-2 uniqueness and indexes, copy every valid legacy row, verify row counts and supported platform directions, and publish version 2 only after all checks pass. Unknown platforms, malformed table shapes, copy collisions, or count mismatches MUST roll back the whole migration.

#### Scenario: One legacy table cannot be copied
- **WHEN** migration encounters an unsupported platform or malformed row in any affected table
- **THEN** the transaction rolls back and all version-1 tables and rows remain unchanged

#### Scenario: Migrated uniqueness would collapse rows
- **WHEN** copied data does not produce the same logical row count under installation-scoped keys
- **THEN** migration rejects the database rather than silently replacing a row

#### Scenario: Migration succeeds
- **WHEN** all version-1 rows have valid pair directions and every verification passes
- **THEN** version 2 becomes visible atomically with all rows available under exact installations

### Requirement: Every Bridge-owned bot identity is installation scoped
Message mappings, message retries, media-group mappings, user caches, sticker caches, QQ-to-Telegram sticker mappings, and heartbeat state SHALL persist the exact relevant installation ids. Their uniqueness, reads, updates, deletes, restore operations, and cache refreshes MUST include those ids and MUST NOT expose a production platform-only repository fallback.

#### Scenario: Equal message ids exist in two pairs
- **WHEN** two source installations persist the same platform and native message id toward two target installations
- **THEN** both mappings coexist and each exact lookup returns only its own target row

#### Scenario: Equal retries exist in two pairs
- **WHEN** two source installations enqueue equal native source ids for different target installations
- **THEN** neither enqueue replaces or completes the other retry

#### Scenario: Provider cache identifiers collide
- **WHEN** two installations use equal user, sticker, file, media-group, or heartbeat identifiers
- **THEN** Bridge reads and updates state only in the requested installation scope

#### Scenario: A QQ sticker is cached for Telegram
- **WHEN** a QQ sticker hash is mapped to a Telegram file id
- **THEN** the cache key includes both source OneBot and target Telegram installations so another pair cannot reuse the file id implicitly

### Requirement: In-memory state uses the same installation scope
Telegram album buffering, retry deduplication, retry callback lookup, and generation-local route caches SHALL include exact installation identity. A callback or buffered item MUST NOT be selected using only `qq`, `telegram`, or the first configured pair.

#### Scenario: Telegram albums share native ids
- **WHEN** two Telegram installations deliver the same chat id and media-group id concurrently
- **THEN** Bridge buffers and flushes two isolated albums to their respective target installations

#### Scenario: A persisted retry is restored
- **WHEN** version-2 retry state is restored after restart or reload
- **THEN** callback selection and send target come from the persisted target installation

### Requirement: Schema migration is restart-gated and observable
A version-1 to version-2 migration SHALL occur only when no previous Bridge generation can access the database. Reload SHALL reject a candidate that would require this migration as restart-required. Migration diagnostics SHALL report schema versions, selected pair id, table names, and bounded row counts but MUST NOT include message content, bot credentials, signed URLs, or complete provider responses.

#### Scenario: Reload candidate sees version 1
- **WHEN** an active Bridge generation exists and a reload candidate would need to migrate version-1 state
- **THEN** reload fails before cutover with a restart-required diagnostic and leaves the active generation and database unchanged

#### Scenario: Startup migrates version 1
- **WHEN** normal process startup has no active Bridge generation and valid legacy-pair input is available
- **THEN** migration may complete before Bridge processes ingress or starts its retry worker

#### Scenario: Migration is logged
- **WHEN** migration succeeds or fails
- **THEN** diagnostics identify version and bounded row-count outcomes without exposing persisted message bodies or provider secrets

### Requirement: Installation scoping does not introduce unrelated persistence systems
The version-2 migration SHALL retain the existing Bridge actor namespace and current Message Store contract. It MUST NOT add a generic outbox, reconciliation journal, blob store, audit event stream, credential table, or Message Store schema migration.

#### Scenario: Existing Message Store is used
- **WHEN** Bridge resolves a stored source or replied message during multi-pair forwarding
- **THEN** it queries the existing `source_bot` and `conversation_id` fields without altering Message Store tables or event types

#### Scenario: Version-2 tables are inspected
- **WHEN** the migrated Bridge database schema is audited
- **THEN** it contains only the version metadata and installation columns/indexes needed by existing Bridge mapping, retry, media, cache, user, and heartbeat behavior
