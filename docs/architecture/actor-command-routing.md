# Actor Command Routing

OBCX observes platform commands at root `RawMessageEvent` ingress and routes
them as typed actor messages. Actors describe message protocols; they do not
publish platform menus, parse platform command syntax, expose handler names, or
register callables with the runtime.

## Actor Declaration

A command request is an ordinary reflected input type:

```cpp
namespace example::commands {
struct PingCommand final
    : obcx::command::RequestMessage<PingCommand> {};
}

class ExampleActor final
    : public obcx::core::ReflectedActor<ExampleActor> {
public:
  static constexpr auto command_contract() {
    return obcx::command::catalog(
        obcx::command::observe<example::commands::PingCommand>(
            "ping", "Check actor availability",
            obcx::command::re2(R"(^(?:ping|health)$)")));
  }

  auto handle(const example::commands::PingCommand &request,
              const obcx::core::MessageEnvelope &message,
              obcx::core::ActorContext &context)
      -> obcx::core::ActorResult;
};
```

`command_contract()` adds command name, description, canonical request message
identity, and an optional RE2 matcher to the existing ABI V2 input contract.
The ordinary name remains the command's stable identity for configuration,
catalogs, diagnostics, processed headers, and actor invocations. A matcher only
adds ways to select that command; it does not replace the name or choose a
handler. The request type must also be present in `accepted_inputs`. Callable,
member-function, handler, platform, and general output metadata are rejected at
contract validation.

The typed request contains `CommandInvocation`: transaction id, normalized
name and arguments, source platform/bot/conversation/sender/message identity,
and retained source event data. Reflected dispatch selects `handle`; that
member function is an actor implementation detail rather than command
registration metadata.

## Route Activation

Declarations do not change data flow until configuration activates them:

```toml
[command_runtime]
timeout_ms = 5000

[[command_runtime.routes]]
actor = "example"
commands = ["ping"]
platforms = ["telegram", "qq"]
bots = ["telegram_bot", "qq_bot"]
fallback = "continue"
# timeout_ms = 10000 # optional route override
```

The runtime expands each route to immutable
`(platform, bot, command) -> (actor, request_type)` entries for one generation.
It validates enabled actors, declared/accepted request types, bot identities,
platform scopes, adapter availability, timeout/fallback values, and duplicate
ownership before actor activation. Startup, reload candidate preparation, and
`--validate-config` use the same validation and perform no command catalog
publication.

`fallback` is `continue` or `consume` and is used for actor failure, malformed
or missing completion, cancellation, and timeout. It does not replace the
actor's successful propagation decision.

Each active matcher is compiled into its generation's immutable routing table.
Patterns use RE2 UTF-8 `FullMatch`, `log_errors = false`, a 4 KiB pattern byte
limit, and a 1 MiB compilation memory limit. Identical active pattern text in
one platform/bot scope is rejected during generation construction.

## Platform Adapter Boundary

`ICommandPlatformAdapter` owns only:

- detection and normalization of the platform's command representation;
- candidate aggregate-catalog validation;
- optional publication of one complete catalog for a bot.

It does not select an actor, construct actor-specific payloads, dispatch a
handler, or own command transactions. Telegram detects leading
`bot_command` entities and honors explicit `@bot` targeting. QQ detects its
supported leading command text. Adapters return only a normalized command
candidate of at most 256 bytes plus the unchanged argument string. A
detection-only adapter remains locally routable even when it cannot publish a
remote catalog.

The coordinator first performs an exact lookup using the canonical candidate.
Only when no exact route exists does it evaluate the bot scope's RE2 patterns.
One full match selects that command and sends its canonical name in
`CommandInvocation`; regex captures are ignored. A partial/substring match is
not enough. If different patterns both match, the coordinator reports
`command_match_ambiguous`, invokes no command actor, applies no route fallback,
and sends the original event through ordinary routing exactly once.

After generation activation, the runtime derives one sorted aggregate catalog
per bot across all active actors. Supported platforms receive a complete
replacement publication with bounded retries. Desired/observed generation,
attempt, retry, and failure status remain generation-owned. Publication
failure does not disable local command routing, and superseded generations
stop retrying. Only canonical names and descriptions enter this catalog;
patterns and inferred aliases are never published.

## Completion And Propagation

A command actor may emit ordinary business messages, but it must also emit
exactly one correlated completion:

```cpp
auto result = obcx::core::ActorResult::success();
result.emit(MyBusinessEvent{}, message);
result.emit(obcx::command::CommandCompleted{
                .transaction_id = request.invocation.transaction_id,
                .propagation = obcx::command::Propagation::Consume,
            },
            message);
return result;
```

- `Consume` completes root processing without starting ordinary raw-event
  pipelines.
- `Continue` resumes the retained original event directly inside the same
  generation. It preserves the original identity and adds reserved
  `obcx.command.*` headers for the processed marker, command, actor,
  transaction, generation, and outcome.

The processed marker makes command detection bypass the continued event.
Ordinary typed emission inherits the headers unless an actor explicitly
replaces them. The message-store actor preserves them on its live
`MessageStored` emission; its current database schema does not persist them
for hypothetical restart replay.

The coordinator rejects missing, duplicate, malformed, wrong-actor,
wrong-generation, or wrong-transaction completion messages. Diagnostics use
stable codes and do not include command payloads.

## Migration From Actor-Local Parsing

For each existing actor command:

1. Define a distinct `RequestMessage` type and add it to
   `command_contract()`.
2. Move command behavior into the corresponding reflected `handle` overload.
3. Emit one correlated `CommandCompleted` with the intended propagation.
4. Add matching `command_runtime.routes` scopes.
5. Remove actor-local slash-prefix/entity parsing and actor-owned replacement
   catalog publication.
6. Keep non-command `RawMessageEvent` behavior, such as mention, reply,
   wake-word, or ordinary message processing, on its existing pipeline.

Pending transactions and all routed descendants retain their admitting
generation. Reload drain therefore either finishes them on that generation or
rejects/times out cutover without transferring work to the candidate.
