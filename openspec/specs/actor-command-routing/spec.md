# actor-command-routing Specification

## Purpose
TBD - created by archiving change add-re2-command-patterns. Update Purpose after archive.
## Requirements
### Requirement: Commands may declare an optional RE2 matcher

The command SDK and runtime SHALL allow an actor command observation to contain
one optional RE2 matcher in addition to its required canonical command name,
description, and typed request message.
The canonical name MUST remain an ordinary valid command string and SHALL
continue to identify configuration activation, routing ownership, command
invocations, processed metadata, diagnostics, and platform catalog entries.
The matcher SHALL add accepted normalized command names without replacing the
canonical exact form.

#### Scenario: Command declares a pattern alias

- **WHEN** an actor declares canonical command `poke` with a valid RE2 matcher that accepts `poke_user`
- **THEN** the generated registration retains canonical name `poke` and adds the matcher metadata for the same typed request message

#### Scenario: Command has no pattern

- **WHEN** an actor declares a command without an RE2 matcher
- **THEN** its contract and runtime behavior remain exact canonical-name matching

#### Scenario: Pattern does not include the canonical name

- **WHEN** an active command's valid RE2 matcher does not match its own canonical name
- **THEN** the canonical name remains accepted through the exact route

### Requirement: RE2 matching follows platform normalization

The platform adapter MUST validate platform command syntax, token boundaries,
and explicit bot targeting before returning a bounded normalized command
candidate. The coordinator MUST apply active RE2 matchers only to the complete
normalized candidate name using RE2 full-match semantics. It MUST NOT apply
actor patterns to raw message text, raw event JSON, arguments, or a command
explicitly targeted at another bot.

#### Scenario: Normalized alias selects a command

- **WHEN** an adapter returns normalized candidate `poke_user`, no exact active route exists, and exactly one active `poke` matcher fully accepts it
- **THEN** the coordinator sends the existing typed `PokeCommand` request with canonical invocation name `poke` and the adapter-parsed arguments

#### Scenario: Pattern matches only a substring

- **WHEN** an active pattern matches only a proper substring of the normalized candidate
- **THEN** the pattern does not select the command

#### Scenario: Command targets another bot

- **WHEN** platform syntax explicitly targets a different bot even though an actor pattern would accept its command token
- **THEN** the adapter reports no candidate for matcher evaluation and ordinary routing remains available

#### Scenario: Exact route also has an overlapping pattern

- **WHEN** a normalized candidate exactly equals an active canonical command and one or more active patterns also accept it
- **THEN** the coordinator selects the exact canonical route without evaluating pattern ownership

### Requirement: Pattern selection is deterministic and generation-safe

The runtime MUST validate matcher metadata, RE2 syntax, pattern size, and
bounded RE2 resource options before actor activation. It SHALL precompile active
patterns into immutable generation-owned command routing state. Identical
active patterns for the same platform and bot scope MUST be rejected before
ingress. If more than one different active pattern matches a candidate for
which no exact route exists, the coordinator MUST report
`command_match_ambiguous`, invoke no command actor, and submit the original
event to ordinary routing exactly once.

#### Scenario: RE2 pattern is invalid

- **WHEN** an actor contract contains malformed RE2 syntax or exceeds a matcher resource limit
- **THEN** ActorManager or candidate validation rejects it before actor activation, ingress, or external catalog mutation

#### Scenario: Reload candidate changes a pattern

- **WHEN** a valid reload candidate changes an active command matcher
- **THEN** the active generation keeps its compiled matcher until successful cutover and subsequent events use only the new generation's matcher

#### Scenario: Two patterns match one candidate

- **WHEN** no exact route exists and two active commands on the same bot scope fully match one normalized candidate
- **THEN** neither actor is invoked, a bounded `command_match_ambiguous` diagnostic is recorded, and the source event enters ordinary routing once

#### Scenario: Active patterns are identical

- **WHEN** two activated command registrations on the same platform and bot scope declare the same RE2 pattern
- **THEN** generation validation rejects the candidate with a stable command-pattern conflict

### Requirement: Regex aliases do not alter platform catalogs

Platform catalog aggregation SHALL publish only each active registration's
canonical command name and description. It MUST NOT publish RE2 pattern text,
derived aliases, or additional entries inferred from a pattern.

#### Scenario: Patterned command is published

- **WHEN** active canonical command `poke` also declares a matcher accepting `poke_user`
- **THEN** the platform aggregate catalog contains `poke` exactly once and contains neither `poke_user` nor the RE2 expression

### Requirement: Unmatched slash-prefixed messages remain ordinary traffic

The generation command coordinator SHALL be the sole authority that converts a
platform command candidate into an intercepted command transaction. When a
candidate has no active route for its platform and bot scope, downstream
pipeline actors MUST treat the original event as ordinary business traffic and
MUST NOT suppress or consume it solely because its raw text begins with `/`.
In particular, a bridge forwarding handler MUST NOT use a raw leading-slash
check as a substitute for the active command routing table.

#### Scenario: An unregistered QQ command-shaped message is bridged

- **WHEN** QQ receives `/tp 2072 ~ 1080`, no active QQ route matches `tp`, and the source group has an enabled bridge mapping
- **THEN** the original message traverses the ordinary message-store and bridge stages once, the target bot is called once, and one forwarding mapping is produced

#### Scenario: An unregistered Telegram command entity is bridged

- **WHEN** Telegram receives a valid leading `bot_command` entity whose normalized name has no active route for that bot and the source group has an enabled bridge mapping
- **THEN** the original message traverses the ordinary pipeline and is forwarded once instead of being rejected by its leading slash

#### Scenario: Slash syntax appears without a platform command match

- **WHEN** a platform adapter reports no command candidate for a slash-prefixed event
- **THEN** downstream bridge processing applies only its ordinary routing, loop, de-duplication, and forwarding rules

#### Scenario: An active command is consumed

- **WHEN** a slash-prefixed event matches an active command route whose valid completion selects `consume`
- **THEN** the command coordinator completes the source operation without submitting it to the ordinary bridge pipeline

#### Scenario: An independent bridge rule rejects the message

- **WHEN** an unmatched slash-prefixed event reaches a bridge whose group mapping is disabled or absent
- **THEN** the bridge may skip it for that explicit routing policy but not because of the slash prefix

### Requirement: Unmatched-command coverage reaches terminal pipeline effects

Automated command-routing coverage SHALL verify the terminal effects of an
unmatched slash-prefixed event across the configured ordinary pipeline, not
only that the coordinator invoked a generic orchestrator. The coverage MUST
use isolated persistence and mock external bot transports.

#### Scenario: The unmatched QQ regression completes

- **WHEN** the end-to-end actor pipeline processes the unregistered QQ message `/tp 2072 ~ 1080`
- **THEN** it verifies message persistence, exactly one target send, a queryable source-to-target mapping, successful forwarding completion, and no missing-mapping bridge failure

#### Scenario: A matched command regression completes

- **WHEN** the same pipeline processes a command that is actively routed and consumed
- **THEN** it verifies the command actor is invoked while the bridge target bot is not called

### Requirement: Platform adapters only translate platform command semantics

The command runtime SHALL provide a generic `ICommandPlatformAdapter` selected
from the configured bot's platform type. An adapter MUST detect and normalize a
platform command from a `RawMessageEvent`, and it MAY support publishing an
aggregate active command catalog. It MUST NOT select a target actor, call an
actor handler, retain command transactions, or decide whether the source event
continues through actor routing.

#### Scenario: Telegram command is normalized

- **WHEN** a Telegram raw event contains a platform-valid command entity targeted at the receiving bot
- **THEN** the Telegram adapter returns the canonical command name, arguments, and source context without invoking an actor

#### Scenario: Platform-specific target does not match

- **WHEN** a platform command explicitly targets a different bot identity
- **THEN** the adapter reports no command and the event remains available to ordinary routing

#### Scenario: Adapter does not support catalog publication

- **WHEN** a platform implementation supports detection but has no remote command-menu API
- **THEN** command routing remains available and catalog publication for that adapter is a supported no-op

### Requirement: Actors declare command observations as message protocols

An actor SHALL declare each command observation as a canonical command name,
human-readable description, and named request message type. The declaration
MUST NOT contain a member-function pointer, callable symbol, handler name, or
direct platform API. Every request type MUST satisfy the SDK command-request
message contract and MUST be present in the same actor's reflected
`accepted_inputs`. A command actor completes the protocol by emitting the
standard `obcx::command::CommandCompleted` message.

#### Scenario: Actor declares two request message types

- **WHEN** an actor declares `chat` mapped to `chat_llm::commands::ChatCommand` and `toggle_think` mapped to `chat_llm::commands::ToggleThinkCommand`
- **THEN** the generated command registrations contain those two message identities and expose no handler or callable metadata

#### Scenario: Reflected request handler is absent

- **WHEN** an actor declares a command request type that is absent from its reflected accepted inputs
- **THEN** the actor fails command-contract generation or loading with an actionable request-type diagnostic

#### Scenario: Actor command names are duplicated

- **WHEN** one actor declares the same canonical command name more than once
- **THEN** its command contract is rejected before actor construction or registration

### Requirement: Configuration explicitly activates actor command routes

An actor command declaration SHALL describe capability only and MUST NOT make
the command active by itself. Runtime configuration SHALL explicitly activate
command names for a target actor and platform/bot scopes and SHALL define a
`continue` or `consume` fallback for transactions that cannot obtain a valid
completion. Only activated registrations SHALL participate in detection,
routing, or platform catalog publication.

#### Scenario: Declared command is not activated

- **WHEN** an actor contract declares `chat` but no command route activates it
- **THEN** `/chat` is not intercepted for that actor and is absent from every published active catalog

#### Scenario: Configured command is activated

- **WHEN** configuration activates `chat` for actor `chat_llm` on a compatible bot scope
- **THEN** the generation routes a normalized `chat` invocation to the request type declared by the `chat_llm` contract

#### Scenario: Route references an undeclared command

- **WHEN** configuration activates a command name not declared by the selected actor
- **THEN** candidate validation fails before ingress or external catalog mutation

### Requirement: Active command routing is validated per generation

The generation builder SHALL combine candidate configuration, loaded actor
contracts, configured bot metadata, and available platform adapters into one
immutable command routing table. It MUST reject an inactive or missing actor, an
undeclared command, a request type absent from accepted inputs, an unknown bot,
a platform/bot mismatch, an unavailable adapter, an invalid fallback, or more
than one active target for the same platform, bot, and canonical command name.

#### Scenario: Two actors claim one scoped command

- **WHEN** candidate configuration activates two actor registrations for the same platform, bot, and canonical command name
- **THEN** the candidate is rejected with a command-route-conflict diagnostic

#### Scenario: Same name is separated by bot scope

- **WHEN** two command routes use the same canonical name on distinct bot identities and every other reference is valid
- **THEN** the generation builds one unambiguous routing entry per bot

#### Scenario: Validation-only startup inspects commands

- **WHEN** `--validate-config` loads actors and a command route is invalid
- **THEN** validation reports the same command-contract or route failure without starting bots, ingress, command transactions, or catalog publication

### Requirement: Command observation sends a typed actor message

For every root `RawMessageEvent`, the generation's command coordinator SHALL
perform command observation before ordinary configured pipelines. An unmatched
event SHALL enter ordinary routing exactly once. For an active match, the
coordinator SHALL create a generation-scoped transaction, retain the source
event, construct the actor-declared request envelope using the SDK's common
command invocation schema, and submit that envelope to the selected actor
through the normal actor scheduler and reflected message dispatcher. The
command runtime MUST NOT directly invoke a handler function.

#### Scenario: Event does not contain an active command

- **WHEN** the selected platform adapter reports no command or the normalized name has no active scoped route
- **THEN** the original `RawMessageEvent` enters its ordinary pipelines once without a command transaction

#### Scenario: Active command reaches its actor

- **WHEN** a raw event normalizes to an active `chat` route
- **THEN** the configured actor receives its declared `ChatCommand` request with transaction identity, normalized name and arguments, source context, and inherited routing metadata

#### Scenario: Request message is unsupported at execution

- **WHEN** a command request reaches an actor generation that does not accept its declared request type despite prior validation
- **THEN** the transaction fails with a stable command-dispatch diagnostic and applies its configured fallback

### Requirement: Command completion controls source propagation

The command actor SHALL emit exactly one `CommandCompleted` message for the
active transaction before its command invocation reaches terminal success.
Completion SHALL identify the transaction and select `continue` or `consume`.
The coordinator MUST accept completion only from the expected actor and
generation. On `continue`, it SHALL resume the retained source event once on
that same generation's ordinary routing path without a second root admission.
On `consume`, it SHALL complete the source event without submitting it to
ordinary pipelines. `CommandCompleted` itself MUST NOT fan out through ordinary
application pipelines.

#### Scenario: Actor continues the source event

- **WHEN** the expected actor emits a valid completion selecting `continue`
- **THEN** the retained raw event enters ordinary routing once in the transaction's generation after command processing

#### Scenario: Actor consumes the source event

- **WHEN** the expected actor emits a valid completion selecting `consume`
- **THEN** the retained raw event does not enter an ordinary pipeline and the root operation completes successfully

#### Scenario: Actor emits other business messages

- **WHEN** a command actor emits application messages in addition to its completion
- **THEN** those application messages follow normal actor routing while only the completion is returned to the command coordinator

#### Scenario: Completion comes from the wrong actor

- **WHEN** a completion references a live transaction but originates from a different actor or generation
- **THEN** the coordinator rejects it, records a bounded protocol diagnostic, and does not change source propagation

### Requirement: Command transactions terminate deterministically

The coordinator SHALL retain pending transaction state and its generation
admission until valid completion, actor failure, cancellation, or a configured
bounded timeout. A successful command invocation with no completion, duplicate
completion, malformed completion, actor failure, timeout, or shutdown
cancellation MUST produce one terminal transaction result and apply the
configured fallback exactly once. Command actors MUST NOT detach completion
work from the scheduler-owned command invocation.

#### Scenario: Actor succeeds without completion

- **WHEN** the command actor invocation reaches terminal success without emitting `CommandCompleted`
- **THEN** the coordinator reports `command_completion_missing` and applies the configured fallback once

#### Scenario: Command actor times out

- **WHEN** the actor invocation does not complete before the route's bounded timeout
- **THEN** the coordinator cancels the invocation, reports `command_timeout`, and applies the configured fallback once

#### Scenario: Actor emits duplicate completion

- **WHEN** one command invocation emits more than one completion for the same transaction
- **THEN** the coordinator accepts at most one propagation decision and reports `command_completion_duplicate`

#### Scenario: Runtime shuts down with a pending command

- **WHEN** shutdown cancels a pending command transaction
- **THEN** its retained source operation receives one terminal result and no waiter or generation reference is abandoned

### Requirement: Continued messages cannot be observed twice

A continued source event SHALL retain its original identity and SHALL carry
reserved command metadata identifying that command observation has completed,
including the canonical command, owner actor, transaction, and outcome. The
command coordinator MUST bypass detection for that marked source event.
Downstream actors MAY inspect or preserve the reserved headers but MUST NOT need
to understand them to receive the event.

#### Scenario: Continued event re-enters ordinary routing

- **WHEN** command completion selects `continue`
- **THEN** ordinary pipelines receive the original event with a processed marker and command metadata and the coordinator does not create a second transaction

#### Scenario: Downstream message preserves headers

- **WHEN** an ordinary actor emits a descendant while preserving parent routing headers
- **THEN** command metadata remains available to later actors without altering the descendant payload schema

### Requirement: Command state follows generation lifecycle

Command routing tables and all pending transaction state SHALL be generation-scoped.
Candidate preparation MUST NOT mutate the active command table or publish an external platform
catalog. A transaction admitted to an old generation and all of its emitted
messages MUST drain against that generation before retirement; a reload MUST
NOT transfer the retained source event or its completion to the new generation.

#### Scenario: Candidate command configuration is invalid

- **WHEN** reload prepares a candidate with an invalid command route
- **THEN** preparation fails while the active command coordinator and published generation remain unchanged

#### Scenario: Command spans reload cutover

- **WHEN** an old-generation command transaction is pending as reload attempts to drain that generation
- **THEN** reload waits for its completion or terminal fallback and does not route its source event through the candidate generation

#### Scenario: Candidate becomes active

- **WHEN** a valid candidate cuts over after the old generation drains
- **THEN** subsequent raw events use only the new immutable command routing table

### Requirement: Platform catalogs aggregate active registrations

For each bot whose adapter supports command-catalog publication, OBCX SHALL
derive one deterministic aggregate catalog from all active scoped command
routes. It MUST publish the complete aggregate rather than allowing individual
actors to replace platform state independently. Reconciliation SHALL begin only
after startup activation or successful generation cutover. Publication failure
MUST leave local routing active, expose degraded status, and use bounded retry
without rolling back to a partially active generation.

#### Scenario: Multiple actors contribute Telegram commands

- **WHEN** two actors have distinct active commands for one Telegram bot
- **THEN** the Telegram adapter receives one sorted aggregate catalog containing both registrations

#### Scenario: Candidate preparation succeeds before cutover

- **WHEN** a candidate command table is valid but has not become active
- **THEN** no platform command-menu mutation occurs

#### Scenario: Remote catalog update fails

- **WHEN** the platform rejects or times out an aggregate catalog publication after activation
- **THEN** local command detection and routing remain active while diagnostics expose the desired catalog, last outcome, and retry state without credentials

### Requirement: Command diagnostics do not expose message contents

Command routing SHALL expose stable failures and bounded telemetry for adapter
detection, active-route lookup, transaction dispatch, completion, propagation,
timeout, and catalog reconciliation. Diagnostics MAY identify generation,
platform, bot, command, actor, request type, transaction, phase, duration, and
stable failure code, but MUST NOT log raw message content, command arguments,
bot credentials, or complete payloads.

#### Scenario: Command dispatch fails

- **WHEN** a command request cannot obtain a valid actor completion
- **THEN** diagnostics identify the safe routing and transaction fields plus a stable failure code without recording arguments or source payload

#### Scenario: Operator inspects catalog status

- **WHEN** aggregate catalog reconciliation is degraded
- **THEN** status identifies the affected platform and bot plus retry timing without exposing bot tokens or proxy credentials
