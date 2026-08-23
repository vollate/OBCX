## MODIFIED Requirements

### Requirement: Current edit, removal, and command behavior uses typed actions
QQ recall propagation, Telegram-origin QQ recall, Telegram text edit, replacement resend, command replies, and OneBot group poke SHALL use the listed typed actions on the exact source/target installations and conversations selected by the invocation's route and complete mapping. Existing `recall`, `checkalive`, and `poke` command completion/propagation and heartbeat-based checkalive text SHALL remain unchanged. Bridge MUST reject ambiguous or route-inconsistent mapping state before any provider side effect and SHALL NOT add a generic command catalog, endpoint-health capability, or provider message lookup.

Bridge-generated QQ-recall replacement text MUST contain no disallowed control character before typed validation and MarkdownV2 escaping. This sanitization SHALL apply to generated replacement text without changing incoming message/media conversion behavior.

#### Scenario: QQ recall mutates its exact Telegram target
- **WHEN** a recall notice from one OneBot installation/group resolves one complete Telegram installation/chat/message mapping
- **THEN** Bridge submits the existing typed delete or edit action to that exact Telegram conversation and applies existing mapping behavior after typed success

#### Scenario: Telegram edit replaces its exact QQ target
- **WHEN** a Telegram edit from one installation/chat resolves one complete OneBot installation/group/message mapping
- **THEN** Bridge deletes and resends only in that mapped QQ conversation and handles typed success/failure without parsing JSON

#### Scenario: Poke command executes
- **WHEN** the existing Telegram `/poke` command resolves a QQ user through the invocation's exact pair and conversations
- **THEN** Bridge submits `onebot11.group.poke` to that pair's OneBot installation/group and preserves current command response behavior

#### Scenario: Checkalive executes
- **WHEN** current checkalive is invoked from one configured source installation/conversation
- **THEN** Bridge reads the selected target installation's heartbeat and sends the response through the source installation's typed group-send action

#### Scenario: Equal target message ids exist in two Telegram chats
- **WHEN** `/recall`, reply resolution, or QQ recall observes the same native Telegram id in the invocation chat and another chat
- **THEN** Bridge uses only the mapping whose target conversation equals the invocation chat

#### Scenario: Destructive resolution is ambiguous
- **WHEN** `/recall`, an edit, or recall propagation cannot prove one mapping whose two conversations agree with the invocation route
- **THEN** Bridge reports `ambiguous_message_mapping` and performs no delete, edit, resend, poke, or mapping mutation

#### Scenario: Generated recall text contains a control character
- **WHEN** sender display data contains a tab, carriage return, NUL, or another disallowed C0 value
- **THEN** Bridge sanitizes generated replacement text before Markdown escaping and dispatch so local operation validation does not fail with a control-character error

### Requirement: Existing Bridge pipeline and storage remain compatible
The conversation-scoped change SHALL continue consuming current `MessageStored` and `RawNoticeEvent` pipeline values and current `common::MessageEvent`/OneBot-shaped segments. It SHALL use existing `source_bot` and `conversation_id` values, preserve current Message Store integration, exact-installation pair routing, generation-owned media-group/retry lifetimes, reload behavior, shutdown behavior, and direct mapping operation-count semantics. Bridge-owned message state SHALL migrate to conversation-scoped version 3, but this change MUST NOT require typed ingress, a Message Store schema/event migration, or another bot action.

#### Scenario: Existing actor pipeline starts
- **WHEN** the current raw-message to Message Store to Bridge pipeline uses valid scalar or named-pair configuration and a valid version-3 database
- **THEN** it remains valid without a new event type, Message Store column, or provider action

#### Scenario: Stored reply data is queried
- **WHEN** Bridge resolves a source or replied message for a formatter, command, recall, or migration path
- **THEN** it queries Message Store by exact source platform, source bot, conversation, and message id without platform-, installation-, or id-only fallback

#### Scenario: Actor generation reloads
- **WHEN** Bridge reloads with pending media groups or retries in an already version-3 database
- **THEN** existing generation ownership rules apply while conversation scope prevents either generation from crossing chats or groups

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

#### Scenario: Same-installation chat collision is simulated
- **WHEN** two Telegram chats on one installation both contain message id `2700` and map to different QQ groups
- **THEN** reply, recall, edit, command, de-duplication, retry, and media paths reach only the complete selected conversation identities

#### Scenario: Ambiguous legacy state is simulated
- **WHEN** a reply or destructive operation sees incomplete version-2 candidates that cannot be assigned one route
- **THEN** the test observes an ambiguity diagnostic and zero provider/mapping side effects

#### Scenario: Installed actor migration suite runs
- **WHEN** Bridge is built against a fresh installed SDK and opens isolated empty, resolvable version-2, ambiguous version-2, archived, and version-3 databases
- **THEN** migration, pipeline forwarding, operation counts, reload gates, and shutdown satisfy the conversation-scoped contract
