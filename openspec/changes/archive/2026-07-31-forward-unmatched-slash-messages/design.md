## Context

The active QQ configuration recognizes only explicitly registered commands.
For an unregistered slash-prefixed message, the current execution path is:

```text
RawMessageEvent "/tp 2072 ~ 1080"
  -> QQ adapter detects candidate "tp"
  -> command routing table has no active "tp" route
  -> CommandCoordinator submits the original event to ordinary routing
  -> message_store persists it and emits MessageStored
  -> BridgeActor invokes QQHandler
  -> QQHandler sees raw_message.starts_with("/") and returns
  -> BridgeForwardingRuntime cannot find a target mapping and reports failure
```

The Telegram handler contains the same independent prefix check. These guards
predate the generation command coordinator and assume that every slash-prefixed
message was already handled as a command. That assumption is now false by
design: adapters may detect a syntactic command candidate that is not present
in the active scoped route table, and unmatched candidates must remain ordinary
traffic.

The production observation confirms this exact boundary. The example `/tp`
message is present in the message-store table, proving it passed command
observation and the persistence stage, while the corresponding bridge stage
reported `bridge forwarding completed without persisted mapping` and created
no forwarding mapping.

## Goals / Non-Goals

**Goals:**

- Forward unmatched leading-slash messages exactly once through configured QQ
  and Telegram bridge routes.
- Keep command recognition scoped to active generation routes rather than raw
  prefix syntax.
- Preserve all existing consumed-command and processed-command behavior.
- Avoid turning an unmatched slash message into a misleading mapping failure.
- Cover the real two-stage `message_store -> bridge` pipeline with mock bot
  transports and an isolated database.

**Non-Goals:**

- Changing which command names are declared or activated.
- Changing the meaning of `Propagation::Continue` or
  `Propagation::Consume` for a matched command.
- Forwarding a command that the coordinator has consumed or a processed command
  that the bridge actor intentionally excludes under its existing policy.
- Removing bridge skips caused by disabled group mappings, unsupported message
  types, loop markers, de-duplication, or other independent business rules.
- Refactoring mapping-result propagation or the
  `bridge forwarding completed without persisted mapping` behavior for every
  other early-return path.
- Adding Redis, changing database durability, or modifying message-store data.

## Decisions

### 1. Treat the active command route as the interception authority

Platform adapters identify syntactic candidates; they do not decide that a
message is owned by a command actor. `CommandCoordinator` combines the
candidate with platform, bot, generation, exact route, and optional matcher
state. Only an active match may create a command transaction and select
`continue` or `consume`.

When no active route matches, the coordinator passes the original event to
ordinary routing without a command transaction or processed marker. Every
downstream stage therefore treats it as business traffic. A bridge handler
must not repeat command detection using only `starts_with("/")`, because that
loses route scope, bot targeting, Telegram entity semantics, regex matching,
and generation ownership.

Teaching the bridge to query the active command catalog was rejected. It would
duplicate coordinator logic, couple a business actor to command-routing
internals, and create reload races between the catalog and the retained
generation route.

### 2. Remove only the unconditional bridge prefix guards

The QQ and Telegram forwarding handlers remove their leading-slash early
returns and stale comments. The message then follows the existing path:

```text
route/config validation
  -> loop prevention
  -> de-duplication
  -> formatting/media processing
  -> target bot send
  -> mapping completion
```

No replacement slash check is added. Prefix syntax is not an independent
bridge skip reason.

The surrounding group/topic enablement, loop markers (`[Telegram]` and `[QQ]`),
message-type checks, and mapping de-duplication remain unchanged. Consequently,
an unmatched slash message can still be skipped for one of those legitimate
reasons, but never solely because its text begins with `/`.

### 3. Leave matched-command propagation behavior unchanged

An active command selecting `consume` never reaches the ordinary pipeline, so
removing the bridge guard cannot forward it. A matched command that reaches
ordinary routing carries the coordinator's reserved processed metadata.
`BridgeActor` already applies its processed-command policy before constructing
the forwarding runtime.

This change does not reinterpret that metadata or alter the command actor's
chosen propagation. Its regression scope is an unmatched event that has no
command transaction and no processed marker.

Using a downstream list of known command names was rejected because it would
be stale across actor reload and would again conflate declaration with active
route configuration.

### 4. Cover QQ and Telegram platform semantics separately

QQ detection uses a leading token, so `/tp 2072 ~ 1080` becomes candidate
`tp`. If `tp` has no active route, the original text and arguments remain part
of the ordinary message and are forwarded unchanged by command routing.

Telegram detection additionally requires a valid `bot_command` entity at
offset zero and respects explicit bot targeting. Whether a slash-prefixed
Telegram message has an unmatched command entity or no command entity, it is
ordinary traffic unless an active scoped route matches it. The Telegram bridge
must therefore remove the same raw-prefix suppression.

### 5. Verify terminal effects, not only coordinator admission

The existing coordinator unit test proves that `/unknown` reaches an ordinary
orchestrator. It did not catch this defect because its test orchestrator is not
the real bridge pipeline.

New tests exercise:

```text
command observation
  -> message_store row
  -> bridge mock-bot send
  -> one source-to-target mapping
  -> MessageForwarded
```

The QQ regression uses `/tp 2072 ~ 1080`. The Telegram regression uses a valid
but unregistered `bot_command` entity. Each asserts one target send and no
`bridge_error`. A matched consumed command remains unsent, and an independently
disabled bridge mapping remains skipped, proving the change does not bypass
either authority.

Tests create unique temporary database paths and never load or mutate the
production database.

## Risks / Trade-offs

- **[Operators relied on all slash messages being hidden]** -> Treat active
  command routes as the explicit interception configuration; unmatched text is
  ordinary traffic by requirement.
- **[A registered command is accidentally forwarded]** -> Retain coordinator
  consume behavior and the bridge actor's processed-metadata gate; add a
  matched-command negative test.
- **[Telegram syntax differs from QQ]** -> Exercise a real Telegram command
  entity and a separate QQ raw token instead of sharing a synthetic detector.
- **[An unrelated early return still reports missing mapping]** -> Keep that
  broader result-model issue in the mapping-roundtrip proposal; this change
  verifies the unmatched slash path actually sends and therefore obtains a
  mapping.
- **[Regression tests touch live data]** -> Use mock bots and a uniquely named
  temporary SQLite database with path guards.

## Migration Plan

1. Add failing QQ and Telegram bridge-pipeline tests for unregistered slash
   messages.
2. Remove the two unconditional bridge prefix guards.
3. Add negative coverage for a consumed active command and an independently
   disabled forwarding mapping.
4. Run command coordinator, bridge actor, installed-SDK pipeline, and strict
   OpenSpec validation.

Rollback restores the two handler guards. No schema, configuration, or data
migration is involved.
