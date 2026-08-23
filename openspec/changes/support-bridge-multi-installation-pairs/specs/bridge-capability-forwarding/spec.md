## MODIFIED Requirements

### Requirement: Bridge uses one explicit QQ and Telegram installation pair
Each Bridge route SHALL use one explicit named pair containing one enabled `telegram.bot_api` installation and one enabled `onebot11.qq` installation. Configuration MAY contain several pairs, but pair ids and installation membership MUST be unique, each active group/topic mapping MUST resolve to exactly one pair, and one source route MUST NOT fan out to several targets. A source event or command MUST carry the matching configured bot name; Bridge MUST NOT fall back to platform-only or first-pair lookup.

The existing scalar `telegram_installation` and `onebot11_installation` fields SHALL remain a single-pair compatibility form when no named-pair collection is present. Pair-less mappings SHALL be accepted only when configuration resolves exactly one pair. Mixing scalar and named-pair forms, omitting a pair in multi-pair mode, reusing an installation in another pair, or naming an unknown pair SHALL fail actor-aware validation.

#### Scenario: Several configured pairs are valid
- **WHEN** Bridge declares two named pairs using four enabled bots of the expected exact surfaces and every mapping resolves to one pair
- **THEN** startup accepts both and builds isolated exact-source routes for each pair

#### Scenario: Source bot selects one pair
- **WHEN** a message, notice, or command names a configured source installation
- **THEN** Bridge considers only that installation's pair and mapped conversation when selecting the target installation

#### Scenario: Source bot is absent or mismatched
- **WHEN** a message, notice, or command lacks `source_bot` or names an installation outside all configured pairs
- **THEN** Bridge returns a route/configuration failure and performs no target provider call

#### Scenario: Pair-less mapping is ambiguous
- **WHEN** more than one pair is configured and a group/topic mapping has no pair id
- **THEN** validation rejects the mapping rather than assigning the first pair

#### Scenario: One installation is reused
- **WHEN** two named pairs contain the same Telegram or OneBot installation
- **THEN** validation rejects the configuration before actor activation

#### Scenario: Existing scalar configuration is used
- **WHEN** Bridge supplies only the current scalar Telegram and OneBot installation fields and current pair-less mappings
- **THEN** they resolve to one compatibility pair with unchanged single-pair routing behavior

### Requirement: Bridge performs bot calls only through the operation client
Bridge forwarding, mutation, current bot-owned media calls, OneBot lookups, poke, command replies, and retry sends SHALL use `BotOperationClient` with the exact target installation selected by the source route or persisted retry. Bridge production code MUST NOT obtain `BotRegistry`, accept `IBot&`, include provider bot interfaces, dynamically cast bots, access connection managers, parse send/delete/edit provider response envelopes, or replace a missing target with another pair.

#### Scenario: Direct forwarding sends a message
- **WHEN** Bridge forwards an existing QQ or Telegram group message from one configured pair
- **THEN** it submits `message.send_group` or `telegram.message.send_topic` to that pair's exact target installation and consumes the typed message reference

#### Scenario: Selected installation is unavailable
- **WHEN** the exact target installation cannot execute the required operation
- **THEN** Bridge returns the typed route/operation failure and does not call another installation of the same platform

#### Scenario: Architecture dependency is scanned
- **WHEN** Bridge production sources and exported link dependencies are inspected after migration
- **THEN** no live bot registry, bot/provider interface, concrete bot, or connection-manager dependency remains

### Requirement: Current media behavior is retained without a new media subsystem
Bridge SHALL preserve current QQ-to-Telegram image/media-group URL and multipart upload paths, Telegram-to-QQ file/sticker/animation paths, caches, temporary files, URL validation, normalization, ffmpeg conversions, fallbacks, and cleanup. Bot-owned Telegram fetch/send and OneBot file-resolution calls SHALL use the exact installation selected for the route. Album buffers and provider-bound cache entries SHALL be installation scoped. No blob gateway or new portable media/loss model SHALL be introduced.

#### Scenario: Telegram media is forwarded to QQ
- **WHEN** current Bridge logic needs Telegram-authenticated file bytes from one source installation
- **THEN** it uses `telegram.media.fetch_file` on that installation and continues its existing conversion/cache/QQ segment path toward the paired OneBot installation

#### Scenario: QQ media group is forwarded to Telegram
- **WHEN** current URL send for one OneBot source falls back to multipart upload
- **THEN** Bridge uses that pair's Telegram installation and preserves existing caption entity, topic, reply, normalization, fallback, and cleanup behavior

#### Scenario: Two Telegram albums have equal native ids
- **WHEN** two Telegram installations receive equal chat and media-group ids during the debounce window
- **THEN** their buffers, flush callbacks, target installations, and persisted media-group mappings remain isolated

#### Scenario: Existing media bound is exceeded
- **WHEN** media exceeds current configured limits
- **THEN** the same bounded failure/fallback policy applies without creating a process blob record

### Requirement: Current edit, removal, and command behavior uses typed actions
QQ recall propagation, Telegram-origin QQ recall, Telegram text edit, replacement resend, command replies, and OneBot group poke SHALL use the listed typed actions on the exact source/target installations selected by the route. Existing `recall`, `checkalive`, and `poke` command completion/propagation and heartbeat-based checkalive text SHALL remain unchanged, with heartbeat reads scoped to the selected installation. Bridge SHALL NOT add a generic command catalog or endpoint-health capability in this change.

#### Scenario: QQ recall removes Telegram target
- **WHEN** a recall notice from one OneBot installation resolves an installation-scoped Telegram mapping
- **THEN** Bridge submits `message.delete` to that mapping's exact Telegram installation and applies existing mapping behavior after typed success

#### Scenario: Telegram edit updates Telegram target
- **WHEN** current QQ edit propagation resolves a mapped Telegram target
- **THEN** Bridge submits `telegram.message.edit_text` to the mapped Telegram installation and handles typed success/failure without parsing JSON

#### Scenario: Poke command executes
- **WHEN** the existing Telegram `/poke` command resolves a QQ user through one configured pair
- **THEN** Bridge submits `onebot11.group.poke` to that pair's OneBot installation and preserves current command response behavior

#### Scenario: Checkalive executes
- **WHEN** current checkalive is invoked from one configured source installation
- **THEN** Bridge reads the selected target installation's heartbeat and sends the response through the source installation's typed group-send action

#### Scenario: Equal message ids exist in another pair
- **WHEN** a recall, edit, or replied command uses an id also present in another pair
- **THEN** Bridge resolves only the mapping and stored message in the invocation's exact installation scope

### Requirement: Existing Bridge pipeline and storage remain compatible
The multi-pair change SHALL continue consuming current `MessageStored` and `RawNoticeEvent` pipeline values and current `common::MessageEvent`/OneBot-shaped segments. It SHALL use existing `source_bot` and `conversation_id` values, preserve current Message Store integration, generation-owned media-group/retry lifetimes, reload behavior, shutdown behavior, and mapping operation-count semantics. Bridge-owned tables SHALL migrate to installation-scoped version 2, but this change MUST NOT require typed ingress or a Message Store schema/event migration.

#### Scenario: Existing actor pipeline starts
- **WHEN** the current raw-message to Message Store to Bridge pipeline uses valid scalar or named-pair configuration
- **THEN** it remains valid and requires no new event type or Message Store schema

#### Scenario: Stored reply data is queried
- **WHEN** Bridge resolves a source or replied message for a command, recall, or formatter path
- **THEN** it queries Message Store by source platform, source bot, conversation, and message id without platform-only fallback

#### Scenario: Actor generation reloads
- **WHEN** Bridge reloads with pending media groups or retries in an already version-2 database
- **THEN** existing generation ownership rules apply while installation scope prevents either generation from crossing pairs

#### Scenario: Reload would migrate version 1
- **WHEN** a candidate generation encounters Bridge schema version 1 while an active generation exists
- **THEN** reload is rejected as restart-required before cutover or database modification

### Requirement: Bridge tests cover only current QQ and Telegram behavior
Isolated tests SHALL cover current bidirectional group/topic text and reply forwarding, media URL/upload/fetch paths, media groups, edits/removals, `recall`/`checkalive`/`poke`, direct mapping operation counts, typed provider errors, uncertain sends, retries, reload, and shutdown across at least two disjoint Telegram/OneBot pairs. Tests SHALL include colliding native ids, wrong source bot, ambiguous config, installation-scoped state, and version-1 migration. They MUST use current fakes/mock transports and isolated databases and MUST NOT require another platform, production credential, fan-out route, outbox, or universal message model.

#### Scenario: Current single-pair simulation runs
- **WHEN** the existing QQ/Telegram Bridge simulation uses the scalar compatibility form and fake operation client
- **THEN** provider calls, forwarding outcomes, mapping writes, retries, commands, and cleanup match the captured baseline

#### Scenario: Multi-pair simulation runs
- **WHEN** two pairs process colliding group, message, media-group, user, sticker, and retry ids
- **THEN** every operation reaches only its configured target installation and all persisted/in-memory outcomes remain isolated

#### Scenario: Installed actor migration suite runs
- **WHEN** Bridge is built against a fresh installed SDK and opens isolated empty, valid legacy, and invalid legacy databases
- **THEN** config validation, schema migration, pipeline forwarding, reload gates, and shutdown satisfy the multi-pair contract

## REMOVED Requirements

### Requirement: Existing direct mapping ownership and schema are preserved
**Reason**: The one-owner write rule remains, but a platform-only schema cannot safely support several bot installations and must be replaced by installation-scoped version 2.

**Migration**: Existing single-pair rows are transactionally assigned to the scalar, sole, or explicitly selected legacy pair before multi-pair activation. Restore the pre-migration database snapshot when rolling back to an older binary.

## ADDED Requirements

### Requirement: Installation-scoped direct mapping ownership is preserved
Bridge SHALL retain its current raw/message-store input conversion, pre-send deduplication, and `BridgeActor` single-owner direct mapping commit while persisting exact source and target installations. A new completed delivery SHALL produce one installation-scoped mapping write before success emission; an already persisted scoped mapping SHALL produce no provider call and no new direct mapping write.

#### Scenario: New direct send succeeds
- **WHEN** exact dispatch returns a valid target message reference for a previously unmapped installation-scoped source
- **THEN** Bridge persists the source and target bot/platform/message identity once and emits success after commit

#### Scenario: Scoped source is already mapped
- **WHEN** current pre-send lookup finds a mapping for the exact source and target installations
- **THEN** Bridge returns the existing forwarding outcome without provider I/O or another direct mapping write

#### Scenario: Equal source exists in another pair
- **WHEN** a platform/message id is mapped in another installation pair but not in the current pair
- **THEN** Bridge treats the current scoped source independently and never reuses the other pair's target

#### Scenario: Source has no enabled mapping
- **WHEN** a valid source event is outside its pair's configured group/topic mappings, its direction is disabled, it is loop-suppressed, or it is accepted by the deferred media-group path
- **THEN** Bridge completes as a no-op without provider I/O, mapping write, or `bridge_not_forwarded` failure

#### Scenario: Mapping write fails
- **WHEN** a provider send completes but the existing direct scoped mapping write fails
- **THEN** Bridge reports mapping persistence failure and does not repeat the send
