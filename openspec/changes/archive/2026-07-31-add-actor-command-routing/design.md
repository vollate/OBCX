## Context

OBCX already has one actor-native message boundary:
`MessageEnvelope` is submitted to an actor through `ActorScheduler`,
`ReflectedActor<Derived>` decodes the canonical input type, and the actor returns
an `ActorResult` containing typed emitted messages. Command handling should use
that boundary rather than add another callback or member-function dispatcher.

Today command-owning actors inspect `RawMessageEvent` themselves. `chat_llm`
parses `/chat` and `/toggle_think`; the bridge has separate Telegram and QQ
parsers for commands such as `/recall`, `/checkalive`, and `/poke`. This creates
three coupled problems:

1. platform syntax and bot targeting are reimplemented per actor;
2. Telegram-style catalog APIs replace the complete remote command list, so
   independent actor publication loses commands;
3. a command handler cannot tell root ingress whether the original event should
   still enter ordinary pipelines.

The command decision is a root data-flow decision. Implementing it as an
ordinary configured pipeline stage would be ambiguous when one raw event feeds
multiple independent pipelines: one stage cannot retroactively consume siblings
that have already started. The observation point therefore belongs immediately
after generation admission and before ordinary pipeline fan-out.

The runtime is generation-scoped and supports transactional reload. A command
transaction that retains the original event must remain a descendant of its
root admission so reload drain cannot retire the actor DSO, command table, or
timeout state while the transaction is pending.

## Goals / Non-Goals

**Goals:**

- Express a command as an actor-owned observation that maps a canonical command
  name to a typed request message.
- Keep command APIs free of function names, member pointers, callbacks, and
  direct actor invocation.
- Keep Telegram/QQ syntax and optional catalog publication behind one generic
  platform adapter interface.
- Make every data-flow-changing command route explicit in configuration and
  validated before ingress.
- Let the actor return a correlated message selecting whether the original
  `RawMessageEvent` continues or is consumed.
- Keep command request, completion, business emissions, scheduler ownership,
  backpressure, diagnostics, and reload drain within the existing actor model.
- Aggregate all active actor registrations before publishing a platform command
  catalog.

**Non-Goals:**

- Exposing handler functions or introducing a second command-specific actor
  dispatcher.
- Inferring commands by scanning arbitrary actor handlers or message output
  sets.
- Supporting runtime registration races through actor-startup
  `RegisterCommand`/`UnregisterCommand` messages in the initial version.
- Making `ICommandPlatformAdapter` own actor routing, pending transactions, or
  propagation decisions.
- Treating command declarations as active without configuration.
- Providing a general RPC framework, distributed reply addresses, or arbitrary
  custom command-completion types.
- Replacing existing nlohmann JSON message serialization.

## Decisions

### 1. Split the platform adapter from the command coordinator

`ICommandPlatformAdapter` is a process capability selected from the bot's
configured platform type. Its detection operation is conceptually:

```cpp
virtual auto detect(
    const RawMessageEvent& event,
    const CommandDetectionContext& context) const
    -> std::optional<DetectedCommand>;
```

`DetectedCommand` contains the canonical name, argument text, platform-specific
source span/target information needed for diagnostics, and the original source
context. Detection is side-effect-free. A separate optional adapter operation
reconciles one complete active catalog for a bot.

The generation-owned `CommandCoordinator` is the observer and transaction
owner. It selects a configured route, retains the source event, constructs the
actor request message, and receives completion. Platform adapters never see
actor identities or command request types.

This split prevents a platform implementation from changing actor topology. It
also lets Telegram and QQ share transaction, timeout, propagation, reload, and
diagnostic behavior while retaining their distinct parsing rules.

### 2. Actor registrations describe messages, not functions

The SDK supplies a command request base/concept with one fixed JSON shape. An
actor creates a named request type so reflected dispatch retains an ordinary,
stable actor-local message identity:

```cpp
namespace chat_llm::commands {

struct ChatCommand final
    : obcx::command::RequestMessage<ChatCommand> {};

struct ToggleThinkCommand final
    : obcx::command::RequestMessage<ToggleThinkCommand> {};

}  // namespace chat_llm::commands
```

The common request data contains:

- generation-scoped transaction identifier;
- normalized command name and arguments;
- source platform, bot, conversation, sender, and message identity;
- the original `RawMessageEvent` data required by command business logic;
- internal reply/correlation metadata inherited by typed emit.

`RequestMessage<Derived>` supplies the standard nlohmann conversion and marker
used by compile-time command-contract validation. The derived type does not add
fields in the initial protocol; actor-specific argument interpretation remains
actor business logic.

The actor declares observations:

```cpp
static constexpr auto command_contract() {
  return command::catalog(
      command::observe<chat_llm::commands::ChatCommand>(
          "chat", "Chat with the LLM"),
      command::observe<chat_llm::commands::ToggleThinkCommand>(
          "toggle_think", "Toggle thinking mode"));
}
```

There is intentionally no handler in this declaration. Because
`ChatCommand` must also appear in the reflected accepted-input set, ordinary
reflected dispatch determines how the actor processes it:

```cpp
ActorTask<ActorResult> handle(
    const chat_llm::commands::ChatCommand& command,
    const MessageEnvelope& envelope,
    ActorContext& context);
```

The `handle` member is an actor implementation detail, not part of the command
contract. A hypothetical
`command::bind<"chat", &ChatLLMActor::on_chat>()` was rejected because it would
duplicate reflected message dispatch and place callable metadata at a
generation/DSO boundary.

### 3. Extend the existing generated contract additively

`ReflectedActor<Derived>::input_contract_json()` adds a deterministic `commands`
array when `Derived::command_contract()` exists:

```json
{
  "schema_version": 1,
  "actor": "chat_llm",
  "accepted_inputs": [
    "chat_llm::commands::ChatCommand",
    "chat_llm::commands::ToggleThinkCommand",
    "obcx::core::events::RawMessageEvent"
  ],
  "commands": [
    {
      "name": "chat",
      "description": "Chat with the LLM",
      "request_type": "chat_llm::commands::ChatCommand"
    },
    {
      "name": "toggle_think",
      "description": "Toggle thinking mode",
      "request_type": "chat_llm::commands::ToggleThinkCommand"
    }
  ]
}
```

The optional additive field remains in contract schema version 1. Existing
actors without a command declaration load with an empty registration set, and
the ABI keeps the same `obcx_get_actor_contract` symbol. The new loader validates
sorted unique names, descriptions, canonical request identities, and membership
in `accepted_inputs`.

The contract still does not declare general outputs. The completion message is
fixed by the command protocol, not an actor-specific output set. Callable,
handler, platform, and completion-function fields are rejected.

### 4. Configuration activates explicit command edges

An actor declaration does not alter running data flow. Configuration activates
selected declarations:

```toml
[command_runtime]
timeout_ms = 5000

[[command_runtime.routes]]
actor = "chat_llm"
commands = ["chat", "toggle_think"]
platforms = ["telegram", "qq"]
bots = ["telegram_bot", "qq_bot"]
fallback = "continue"
```

The route is the explicit ingress edge:

```text
(platform, bot, command)
  -> actor
  -> actor-declared request_type
  -> CommandCompleted
  -> continue | consume source RawMessageEvent
```

The request type is not repeated in configuration because the actor contract is
its authoritative owner. The adapter implementation is not named in the route;
the configured bot platform selects it. This prevents configuration from
remapping a command to an arbitrary input type or selecting a platform adapter
that disagrees with the bot transport.

Candidate generation validation expands route command lists and rejects:

- missing or disabled actors;
- undeclared commands;
- request types outside accepted inputs;
- missing bots or mismatched platform/bot scopes;
- unavailable platform adapters;
- invalid timeouts or fallback policies;
- duplicate `(platform, bot, command)` active keys.

The result is an immutable table owned by the candidate generation. Explicit
lists are required initially; implicit activation of every actor declaration is
excluded.

### 5. Use an actor-message request/completion transaction

The root sequence is:

```text
RawMessageEvent admitted to generation
  -> CommandCoordinator observes through platform adapter
     -> no active match: ordinary pipelines
     -> active match: retain source and create transaction
        -> typed command request to target ActorScheduler mailbox
        -> actor returns ActorResult
           -> ordinary emitted messages route normally
           -> CommandCompleted returns to coordinator
        -> Continue: same-generation ordinary pipelines
        -> Consume: terminal root result
```

The request envelope has the actor-declared canonical type and the standard
request JSON payload. It carries reserved headers for transaction identity,
expected owner, generation, and an internal reply destination. Submission uses
the same scheduler, mailbox, backpressure, reflected JSON dispatch, and DSO
lifetime retention as every other actor message.

The actor returns completion as a typed emitted message:

```cpp
ActorResult result;
result.emit(
    command::CommandCompleted{
        .transaction_id = command.transaction_id(),
        .propagation = command::Propagation::Consume,
    },
    envelope);
co_return result;
```

The coordinator's directed completion route is generated with the active
command binding. The runtime extracts `CommandCompleted` from the terminal
actor result and delivers it only to that generation's coordinator; it is not
offered to application pipelines. Typed emit inherits the transaction and
reply headers, and the coordinator independently checks transaction,
generation, and expected actor before accepting it.

Other messages emitted by the actor remain ordinary actor outputs. This keeps
user-visible replies, storage requests, bridge events, and other business
effects out of the control protocol.

Directly awaiting the scheduler-owned actor invocation does not expose a
function call: the coordinator submits a typed envelope and receives an emitted
typed envelope. The target actor continues to be selected by configuration and
dispatched by the actor runtime.

### 6. Make source propagation one terminal transaction decision

`CommandCompleted` contains one of:

```cpp
enum class Propagation {
  Continue,
  Consume,
};
```

For `Continue`, the coordinator resumes the retained source event directly on
the ordinary route entry of the same generation. It does not submit it to the
root reload gate again. Reserved headers record:

- `obcx.command.processed`;
- `obcx.command.name`;
- `obcx.command.actor`;
- `obcx.command.transaction`;
- `obcx.command.outcome`.

The processed marker bypasses command detection and prevents a loop.
Descendants inherit these headers through the existing typed-emit behavior
unless an actor explicitly replaces headers.

For `Consume`, no ordinary raw-event pipeline starts. The command actor's
ordinary emitted business messages still route.

The direct command invocation must emit exactly one completion before terminal
success. The coordinator handles exceptional paths as follows:

| Condition | Result |
| --- | --- |
| Actor failure | record `command_actor_failure`, apply route fallback |
| Success without completion | record `command_completion_missing`, apply fallback |
| Duplicate completion | accept no more than one decision, record `command_completion_duplicate` |
| Malformed/wrong owner completion | reject it, then fail or time out transaction |
| Bounded timeout | cancel actor work, record `command_timeout`, apply fallback |
| Runtime cancellation | publish one terminal root result and release state |

Fallback is configuration, not an actor default, because it applies only when
the actor protocol did not produce a trustworthy decision. Actor code must keep
completion within its scheduler-owned task; detached completion would escape
generation drain and is unsupported.

### 7. Keep command state inside runtime generations

Each `RuntimeGeneration` owns:

- the immutable active command routing table;
- coordinator transaction state;
- timeout/cancellation state;
- the generation-specific view of adapter capabilities;
- the desired aggregate catalog derived from active routes.

A root command transaction retains its generation admission until completion or
fallback. Its request, application emissions, completion, and continued source
are routed descendants of that admission. Old-generation drain therefore waits
for them and cannot move the retained event to a candidate generation.

Candidate preparation parses contracts and config and builds the candidate
table without publishing menus or changing active adapter state. Invalid
candidates are discarded. After successful cutover, the new generation becomes
authoritative for detection and an asynchronous catalog reconciliation begins.

Process-owned bot connections remain outside generations. Catalog publication
uses the process-owned bot resolved from `BotRegistry`, but desired catalog and
retry ownership are tied to the active generation so a superseded retry cannot
publish an obsolete catalog.

### 8. Publish one aggregate platform catalog per bot

The runtime derives a sorted catalog from active routes and their actor
descriptions. It calls the platform adapter once with the complete desired
catalog for the bot. This is required for replacement APIs such as Telegram
`setMyCommands`; actors never call that API independently.

Adapters report whether publication is supported. Detection-only adapters
accept the local routing table without remote mutation. Publication starts only
after initial activation or reload cutover. A remote failure does not roll back
local actor routing because the platform side effect cannot be made atomic with
generation swap; instead the runtime records desired/last-observed state and
performs bounded retry while the generation remains active.

Candidate validation asks the adapter to validate names/descriptions against
platform constraints without contacting the platform. A registration valid for
one platform but invalid for another makes only the candidate route containing
the incompatible scope invalid.

### 9. Prefer generation-time registration over actor-startup messages

An alternative design lets actors emit `RegisterCommand` and
`UnregisterCommand` after startup. It was rejected for the initial capability:

- ingress could begin before every actor registration arrives;
- duplicate ownership would become a runtime race rather than validation;
- reload would need to distinguish late old-generation registrations;
- platform catalogs could flap while actors start;
- validation-only startup could not prove the final command table.

`command_contract()` is therefore the actor's registration surface, and config
activation plus generation build is the atomic subscription point. This is
still an actor message protocol at execution time; static registration only
describes which typed request the actor agrees to observe.

### 10. Keep diagnostics bounded and payload-free

Command telemetry records safe identifiers and phases: generation, platform,
bot, canonical command, target actor, request type, transaction, propagation,
latency, fallback, and stable failure code. Raw text, parsed arguments,
embedded `RawMessageEvent`, credentials, and complete JSON payloads are never
logged.

Catalog status distinguishes desired generation, last attempted generation,
last success, retry count, and safe platform failure code. This makes local
routing and remote menu drift independently observable.

## Risks / Trade-offs

- **[The coordinator delays ordinary routing while an actor handles a command]**
  → Require bounded per-route timeouts, scheduler cancellation, backpressure,
  and an explicit fallback.
- **[A command request type has a fixed common payload shape]** → Use distinct
  named request types for actor dispatch but keep actor-specific argument
  parsing inside the actor; defer arbitrary request factories because they
  reintroduce callable dispatch.
- **[A continued event could cross reload into a new generation]** → Retain the
  original generation admission and resume at that generation's ordinary route
  entry rather than root ingress.
- **[An actor emits completion from detached work]** → Require completion in the
  scheduler-owned command invocation and cover timeout, cancellation, reload,
  TSan, and ASan/UBSan cases.
- **[Remote command menus temporarily differ from active local routing]** →
  Publish only after activation, expose desired/observed status, cancel
  superseded retries, and retry without weakening local routing.
- **[A platform parser treats command prefixes as exact commands]** → Put token
  boundary, entity, bot-target, and normalization rules in adapter-specific
  tests rather than actor string-prefix checks.
- **[A global coordinator serializes unrelated commands]** → Partition
  coordinator work by bot/conversation or transaction while retaining
  per-source ordering and generation ownership.
- **[The additive actor contract is mistaken for an output contract]** → Store
  only command metadata and request types; keep `CommandCompleted` fixed by the
  core protocol and reject general output/callable fields.

## Migration Plan

1. Add SDK command request/completion types, compile-time command declarations,
   generated contract metadata, and loader validation while treating actors
   without commands as an empty registration set.
2. Add config parsing, platform adapter registry, candidate command-table
   validation, and validation-only diagnostics without enabling interception.
3. Add the coordinator transaction path, directed completion routing,
   propagation marker, timeout/fallback behavior, and generation drain tests.
4. Implement Telegram and QQ detection adapters; add aggregate Telegram catalog
   reconciliation after activation.
5. Migrate `obcx-actor-bridge` commands first. Return `Continue` where the
   original event must still pass through message-store, and make its later
   `MessageStored` handling recognize processed command metadata so it neither
   executes nor forwards the command twice.
6. Migrate `chat_llm` `/chat` and `/toggle_think` to distinct request messages
   while retaining mention, reply, wake-word, and proactive behavior on
   `RawMessageEvent`.
7. Add message-store header-preservation coverage and update the actor template
   with a command observation example.
8. Remove actor-local platform command parsing and independent remote catalog
   publication after every migrated command has an active config route.

Rollback is deployment of the preceding runtime together with its matching
actor binaries and configuration. During a staged migration, command
interception must not be activated for a command until its target actor accepts
the declared request type.

## Open Questions

None for the initial capability. Dynamic registrations, localized catalog
descriptions, arbitrary typed argument decoding, and custom completion payloads
require separate proposals.
