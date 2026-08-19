## ADDED Requirements

### Requirement: Bridge uses one explicit QQ and Telegram installation pair
Bridge configuration SHALL require `telegram_installation` and `onebot11_installation`. Actor-aware validation MUST verify that the named enabled bots exist with `telegram.bot_api` and `onebot11.qq`, respectively, and MUST reject a Bridge configuration that attempts to use more than one pair. A source event or command MUST carry the matching configured bot name; Bridge MUST NOT fall back to platform-only lookup.

#### Scenario: Configured pair is valid
- **WHEN** Bridge names one enabled Telegram bot and one enabled OneBot bot with the expected surfaces
- **THEN** all source and target operations use those exact installation ids

#### Scenario: Source bot is absent or mismatched
- **WHEN** a message, notice, or command lacks `source_bot` or names another installation
- **THEN** Bridge returns a route/configuration failure and performs no target provider call

#### Scenario: Another pair is requested
- **WHEN** configuration attempts to route Bridge mappings through a second Telegram or OneBot installation pair
- **THEN** validation rejects it and explains that multi-pair Bridge mapping is outside this change

### Requirement: Bridge performs bot calls only through the operation client
Bridge forwarding, mutation, current bot-owned media calls, OneBot lookups, poke, command replies, and retry sends SHALL use `BotOperationClient`. Bridge production code MUST NOT obtain `BotRegistry`, accept `IBot&`, include provider bot interfaces, dynamically cast bots, access connection managers, or parse send/delete/edit provider response envelopes.

#### Scenario: Direct forwarding sends a message
- **WHEN** Bridge forwards an existing QQ or Telegram group message
- **THEN** it submits `message.send_group` or `telegram.message.send_topic` to the configured installation and consumes the typed message reference

#### Scenario: Architecture dependency is scanned
- **WHEN** Bridge production sources and exported link dependencies are inspected after migration
- **THEN** no live bot registry, bot/provider interface, concrete bot, or connection-manager dependency remains

### Requirement: Existing direct mapping ownership and schema are preserved
Bridge SHALL retain its current raw/message-store input conversion, pre-send deduplication, and `BridgeActor` single-owner direct mapping commit. A new completed delivery SHALL produce one existing-format mapping write before success emission; an already persisted mapping SHALL produce no provider call and no new direct mapping write. This change MUST NOT add or migrate Bridge database tables.

#### Scenario: New direct send succeeds
- **WHEN** typed dispatch returns a valid target message reference for a previously unmapped source
- **THEN** Bridge persists the same source/target platform and message-id mapping once and emits success after commit

#### Scenario: Source is already mapped
- **WHEN** current pre-send lookup finds a mapping
- **THEN** Bridge returns the existing forwarding outcome without provider I/O or another direct mapping write

#### Scenario: Source has no enabled mapping
- **WHEN** a valid source event is outside the configured group/topic mappings, its direction is disabled, it is loop-suppressed, or it is accepted by the deferred media-group path
- **THEN** Bridge completes as a no-op without provider I/O, mapping write, or `bridge_not_forwarded` failure

#### Scenario: Mapping write fails
- **WHEN** a provider send completes but the existing direct mapping write fails
- **THEN** Bridge reports mapping persistence failure and does not repeat the send

#### Scenario: Existing database is opened
- **WHEN** the narrowed refactor runs against current Bridge tables
- **THEN** no v2 mapping, journal, outbox, blob, installation-column, audit, or rollback-projection migration is required

### Requirement: Current media behavior is retained without a new media subsystem
Bridge SHALL preserve current QQ-to-Telegram image/media-group URL and multipart upload paths, Telegram-to-QQ file/sticker/animation paths, caches, temporary files, URL validation, normalization, ffmpeg conversions, fallbacks, and cleanup. Only bot-owned Telegram fetch/send and OneBot file-resolution calls SHALL move behind the operation client. No blob gateway or new portable media/loss model SHALL be introduced.

#### Scenario: Telegram media is forwarded to QQ
- **WHEN** current Bridge logic needs Telegram-authenticated file bytes
- **THEN** it uses `telegram.media.fetch_file` and continues its existing conversion/cache/QQ segment path

#### Scenario: QQ media group is forwarded to Telegram
- **WHEN** current URL send falls back to multipart upload
- **THEN** Bridge uses the typed Telegram URL/upload media actions and preserves existing caption entity, topic, reply, normalization, fallback, and cleanup behavior

#### Scenario: Existing media bound is exceeded
- **WHEN** media exceeds current configured limits
- **THEN** the same bounded failure/fallback policy applies without creating a process blob record

### Requirement: Current edit, removal, and command behavior uses typed actions
QQ recall propagation, Telegram-origin QQ recall, Telegram text edit, replacement resend, command replies, and OneBot group poke SHALL use the listed typed actions. Existing `recall`, `checkalive`, and `poke` command completion/propagation and heartbeat-based checkalive text SHALL remain unchanged. Bridge SHALL NOT add a generic command catalog or endpoint-health capability in this change.

#### Scenario: QQ recall removes Telegram target
- **WHEN** the current notice path resolves a mapped Telegram message
- **THEN** Bridge submits `message.delete` with the separate Telegram chat/message reference and applies existing mapping behavior after typed success

#### Scenario: Telegram edit updates Telegram target
- **WHEN** current QQ edit propagation can edit its mapped Telegram target
- **THEN** Bridge submits `telegram.message.edit_text` and handles typed success/failure without parsing JSON

#### Scenario: Poke command executes
- **WHEN** the existing Telegram `/poke` command resolves a QQ user
- **THEN** Bridge submits `onebot11.group.poke` to the configured OneBot installation and preserves current command response behavior

#### Scenario: Checkalive executes
- **WHEN** current checkalive is invoked
- **THEN** Bridge continues using its existing heartbeat repository and sends the response through the typed group-send action

### Requirement: Typed failures preserve existing mapping and retry safety
Bridge SHALL create forwarding success or a target mapping only from a successful typed send containing a valid target message reference. It SHALL enqueue automatic message retry only for a retryable `DefinitelyNotSubmitted` error. A `PossiblySubmitted` result MUST produce no fabricated mapping, success, or automatic resend.

#### Scenario: Provider rejects before submission is accepted
- **WHEN** the wrapper returns a retryable definitely-not-submitted send failure and retry is enabled
- **THEN** Bridge enqueues one entry through its existing retry manager

#### Scenario: Send result is uncertain
- **WHEN** dispatch reports a possibly-submitted side effect
- **THEN** Bridge reports the uncertain failure and neither maps nor automatically retries it

### Requirement: Existing Bridge pipeline and storage remain compatible
The narrowed refactor SHALL continue consuming current `MessageStored` and `RawNoticeEvent` pipeline values and current `common::MessageEvent`/OneBot-shaped segments. It SHALL preserve current message-store integration, repository operation counts, generation-owned media-group/retry lifetimes, reload behavior, and shutdown behavior. It MUST NOT require typed ingress or message-store migration.

#### Scenario: Existing actor pipeline starts
- **WHEN** the current raw-message to message-store to Bridge pipeline is configured with the new installation pair
- **THEN** it remains valid and does not require a new event type or schema

#### Scenario: Actor generation reloads
- **WHEN** Bridge reloads with pending media groups or retries
- **THEN** existing generation ownership rules apply while both generations use the shared operation client

### Requirement: Bridge tests cover only current QQ and Telegram behavior
Isolated tests SHALL cover current bidirectional group/topic text and reply forwarding, media URL/upload/fetch paths, media groups, edits/removals, `recall`/`checkalive`/`poke`, direct mapping operation counts, typed provider errors, uncertain sends, retries, reload, and shutdown. Tests MUST use current fakes/mock transports and isolated databases and MUST NOT require another platform or multi-pair mapping schema.

#### Scenario: Current business simulation runs
- **WHEN** the existing QQ/Telegram Bridge simulation is switched to a fake operation client
- **THEN** provider calls, forwarding outcomes, mapping writes, retries, commands, and cleanup match the captured baseline
