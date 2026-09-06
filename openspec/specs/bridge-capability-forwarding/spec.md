# bridge-capability-forwarding Specification

## Purpose
Define Bridge forwarding through the narrow bot-operation client while preserving existing QQ/Telegram behavior, persistence ownership, media processing, and retry safety.

## Requirements
### Requirement: Bridge uses one explicit QQ and Telegram installation pair
Each Bridge route SHALL use one explicit named pair containing one enabled `telegram.bot_api` installation and one enabled `onebot11.qq` installation. Configuration MAY contain several pairs, but pair ids and installation membership MUST be unique, each active group/topic mapping MUST resolve to exactly one pair, and one source route MUST NOT fan out to several targets. A source event or command MUST carry the matching configured bot name; Bridge MUST NOT fall back to platform-only or first-pair lookup.

The existing scalar `telegram_installation` and `onebot11_installation` fields SHALL remain a single-pair compatibility form when no named-pair collection is present. Pair-less mappings SHALL be accepted only when configuration resolves exactly one pair. Mixing scalar and named-pair forms, omitting a pair in multi-pair mode, reusing an installation in another pair, or naming an unknown pair SHALL fail actor-aware validation.

#### Scenario: Source bot is absent or mismatched
- **WHEN** a message, notice, or command lacks `source_bot` or names an installation outside all configured pairs
- **THEN** Bridge returns a route/configuration failure and performs no target provider call

#### Scenario: Several configured pairs are valid
- **WHEN** Bridge declares two named pairs using four enabled bots of the expected exact surfaces and every mapping resolves to one pair
- **THEN** startup accepts both and builds isolated exact-source routes for each pair

#### Scenario: Source bot selects one pair
- **WHEN** a message, notice, or command names a configured source installation
- **THEN** Bridge considers only that installation's pair and mapped conversation when selecting the target installation

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

#### Scenario: Architecture dependency is scanned
- **WHEN** Bridge production sources and exported link dependencies are inspected after migration
- **THEN** no live bot registry, bot/provider interface, concrete bot, or connection-manager dependency remains

#### Scenario: Selected installation is unavailable
- **WHEN** the exact target installation cannot execute the required operation
- **THEN** Bridge returns the typed route/operation failure and does not call another installation of the same platform

### Requirement: Current media behavior is retained without a new media subsystem
Bridge SHALL preserve current QQ-to-Telegram image/media-group URL and multipart upload paths, Telegram-to-QQ file/sticker/animation paths, caches, temporary files, URL validation, normalization, ffmpeg conversions, fallbacks, and cleanup. Bot-owned Telegram fetch/send and OneBot file-resolution calls SHALL use the exact installation selected for the route. Album buffers and provider-bound cache entries SHALL be installation scoped. No blob gateway or new portable media/loss model SHALL be introduced.

#### Scenario: Telegram media is forwarded to QQ
- **WHEN** current Bridge logic needs Telegram-authenticated file bytes from one source installation
- **THEN** it uses `telegram.media.fetch_file` on that installation and continues its existing conversion/cache/QQ segment path toward the paired OneBot installation

#### Scenario: QQ media group is forwarded to Telegram
- **WHEN** current URL send for one OneBot source falls back to multipart upload
- **THEN** Bridge uses that pair's Telegram installation and preserves existing caption entity, topic, reply, normalization, fallback, and cleanup behavior

#### Scenario: Existing media bound is exceeded
- **WHEN** media exceeds current configured limits
- **THEN** the same bounded failure/fallback policy applies without creating a process blob record

#### Scenario: Two Telegram albums have equal native ids
- **WHEN** two Telegram installations receive equal chat and media-group ids during the debounce window
- **THEN** their buffers, flush callbacks, target installations, and persisted media-group mappings remain isolated

### Requirement: Current edit, removal, and command behavior uses typed actions
QQ recall propagation, Telegram-origin QQ recall, Telegram text edit, replacement resend, command replies, and OneBot group poke SHALL use the listed typed actions on the exact source/target installations and conversations selected by the invocation's route and complete mapping. Existing `recall`, `checkalive`, and `poke` command completion/propagation and heartbeat-based checkalive text SHALL remain unchanged. Bridge MUST reject ambiguous or route-inconsistent mapping state before any provider side effect and SHALL NOT add a generic command catalog, endpoint-health capability, or provider message lookup.

Bridge-generated QQ-recall replacement text MUST contain no disallowed control character before typed validation and MarkdownV2 escaping. This sanitization SHALL apply to generated replacement text without changing incoming message/media conversion behavior.

#### Scenario: Poke command executes
- **WHEN** the existing Telegram `/poke` command resolves a QQ user through the invocation's exact pair and conversations
- **THEN** Bridge submits `onebot11.group.poke` to that pair's OneBot installation/group and preserves current command response behavior

#### Scenario: Checkalive executes
- **WHEN** current checkalive is invoked from one configured source installation/conversation
- **THEN** Bridge reads the selected target installation's heartbeat and sends the response through the source installation's typed group-send action

#### Scenario: Equal message ids exist in another pair
- **WHEN** a recall, edit, or replied command uses an id also present in another pair
- **THEN** Bridge resolves only the mapping and stored message in the invocation's exact installation scope

#### Scenario: QQ recall mutates its exact Telegram target
- **WHEN** a recall notice from one OneBot installation/group resolves one complete Telegram installation/chat/message mapping
- **THEN** Bridge submits the existing typed delete or edit action to that exact Telegram conversation and applies existing mapping behavior after typed success

#### Scenario: Telegram edit replaces its exact QQ target
- **WHEN** a Telegram edit from one installation/chat resolves one complete OneBot installation/group/message mapping
- **THEN** Bridge deletes and resends only in that mapped QQ conversation and handles typed success/failure without parsing JSON

#### Scenario: Equal target message ids exist in two Telegram chats
- **WHEN** `/recall`, reply resolution, or QQ recall observes the same native Telegram id in the invocation chat and another chat
- **THEN** Bridge uses only the mapping whose target conversation equals the invocation chat

#### Scenario: Destructive resolution is ambiguous
- **WHEN** `/recall`, an edit, or recall propagation cannot prove one mapping whose two conversations agree with the invocation route
- **THEN** Bridge reports `ambiguous_message_mapping` and performs no delete, edit, resend, poke, or mapping mutation

#### Scenario: Generated recall text contains a control character
- **WHEN** sender display data contains a tab, carriage return, NUL, or another disallowed C0 value
- **THEN** Bridge sanitizes generated replacement text before Markdown escaping and dispatch so local operation validation does not fail with a control-character error

### Requirement: Typed failures preserve existing mapping and retry safety
Bridge SHALL create forwarding success or a target mapping only from a successful typed send containing a valid target message reference. It SHALL enqueue automatic message retry only for a retryable `DefinitelyNotSubmitted` error. A `PossiblySubmitted` result MUST produce no fabricated mapping, success, or automatic resend.

#### Scenario: Provider rejects before submission is accepted
- **WHEN** the wrapper returns a retryable definitely-not-submitted send failure and retry is enabled
- **THEN** Bridge enqueues one entry through its existing retry manager

#### Scenario: Send result is uncertain
- **WHEN** dispatch reports a possibly-submitted side effect
- **THEN** Bridge reports the uncertain failure and neither maps nor automatically retries it

### Requirement: Existing Bridge pipeline and storage remain compatible
The conversation-scoped change SHALL continue consuming current `MessageStored` and `RawNoticeEvent` pipeline values and current `common::MessageEvent`/OneBot-shaped segments. It SHALL use existing `source_bot` and `conversation_id` values, preserve current Message Store integration, exact-installation pair routing, generation-owned media-group/retry lifetimes, reload behavior, shutdown behavior, and direct mapping operation-count semantics. Bridge-owned message state SHALL migrate to conversation-scoped version 3, but this change MUST NOT require typed ingress, a Message Store schema/event migration, or another bot action.

#### Scenario: Existing actor pipeline starts
- **WHEN** the current raw-message to Message Store to Bridge pipeline uses valid scalar or named-pair configuration and a valid version-3 database
- **THEN** it remains valid without a new event type, Message Store column, or provider action

#### Scenario: Actor generation reloads
- **WHEN** Bridge reloads with pending media groups or retries in an already version-3 database
- **THEN** existing generation ownership rules apply while conversation scope prevents either generation from crossing chats or groups

#### Scenario: Stored reply data is queried
- **WHEN** Bridge resolves a source or replied message for a formatter, command, recall, or migration path
- **THEN** it queries Message Store by exact source platform, source bot, conversation, and message id without platform-, installation-, or id-only fallback

#### Scenario: Reload would migrate version 1
- **WHEN** a candidate generation encounters Bridge schema version 1 while an active generation exists
- **THEN** reload is rejected as restart-required before cutover or database modification

#### Scenario: Reload would migrate version 2
- **WHEN** a candidate generation encounters Bridge schema version 2 while an active generation exists
- **THEN** reload is rejected as restart-required before cutover or database modification

#### Scenario: Existing media behavior runs
- **WHEN** a conversation-scoped route forwards current QQ or Telegram media
- **THEN** current conversion, URL/upload/fetch, temporary-file, fallback, and cleanup behavior remains unchanged

### Requirement: Bridge tests cover only current QQ and Telegram behavior
Isolated tests SHALL cover current bidirectional group/topic text and reply forwarding, media URL/upload/fetch paths, media groups, edits/removals, `recall`/`checkalive`/`poke`, direct mapping operation counts, typed provider errors, uncertain sends, retries, reload, and shutdown across disjoint installation pairs and colliding conversations. Tests SHALL reproduce same-installation equal message ids, version-2 migration classification, route history, strict failure, explicit archive, and generated recall-text sanitization. They MUST use current fakes/mock transports and isolated databases and MUST NOT require another platform, production credential/database, fan-out route, outbox, provider message lookup, or universal message model.

#### Scenario: Current single-pair simulation runs
- **WHEN** the existing QQ/Telegram Bridge simulation uses scalar compatibility configuration and fake operation client
- **THEN** provider calls, forwarding outcomes, exact mapping writes, retries, commands, and cleanup match the captured baseline

#### Scenario: Multi-pair simulation runs
- **WHEN** two pairs process colliding group, message, media-group, user, sticker, and retry ids
- **THEN** every operation reaches only its configured target installation and all persisted/in-memory outcomes remain isolated

#### Scenario: Installed actor migration suite runs
- **WHEN** Bridge is built against a fresh installed SDK and opens isolated empty, resolvable version-2, ambiguous version-2, archived, and version-3 databases
- **THEN** migration, pipeline forwarding, operation counts, reload gates, and shutdown satisfy the conversation-scoped contract

#### Scenario: Same-installation chat collision is simulated
- **WHEN** two Telegram chats on one installation both contain message id `2700` and map to different QQ groups
- **THEN** reply, recall, edit, command, de-duplication, retry, and media paths reach only the complete selected conversation identities

#### Scenario: Ambiguous legacy state is simulated
- **WHEN** a reply or destructive operation sees incomplete version-2 candidates that cannot be assigned one route
- **THEN** the test observes an ambiguity diagnostic and zero provider/mapping side effects

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
