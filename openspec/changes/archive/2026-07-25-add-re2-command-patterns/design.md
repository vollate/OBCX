## Context

`add-actor-command-routing` defines a stable canonical command name in each
actor observation, exact `(platform, bot, command)` route keys, platform-owned
syntax normalization, and typed actor request/completion messages. That design
keeps actor routing deterministic, but one registration cannot recognize an
alias without declaring another canonical command and request mapping.

RE2 is already a required core dependency and is used elsewhere in OBCX. The
extension therefore needs no new regex engine, but it crosses SDK contract
generation, actor contract loading, generation construction, platform command
normalization, runtime matching, catalog publication, diagnostics, and reload
lifetime boundaries.

The regex must not undo the existing separation of responsibilities. Platform
adapters remain responsible for recognizing platform command syntax and
explicit bot targets. Actors still describe message protocols rather than
handlers. The coordinator remains the only component that selects an active
route and controls the original message's data flow.

## Goals / Non-Goals

**Goals:**

- Let one actor command declaration optionally attach an RE2 pattern.
- Keep the required command name as the stable canonical string.
- Match aliases without changing the command request type or completion
  protocol.
- Preserve deterministic exact matching and fail safely on ambiguous regex
  matches.
- Validate and precompile bounded patterns before a generation accepts ingress.
- Keep matcher state and diagnostics generation-owned and reload-safe.

**Non-Goals:**

- Treating a regex as a command name or publishing regex text to platform
  command catalogs.
- Matching arbitrary raw event JSON, message bodies, argument text, or bot
  credentials.
- Moving platform parsing or explicit bot-target validation into actors.
- Exposing regex captures in `CommandInvocation` or generating actor-specific
  request payloads.
- Adding route priority, first-declaration-wins behavior, dynamic registration,
  or a second actor dispatch mechanism.
- Supporting `std::regex`, PCRE, or configurable regex engines.

## Decisions

### 1. Add an optional RE2 matcher to the actor command observation

The SDK extends the existing observation helper with an optional matcher:

```cpp
static constexpr auto command_contract() {
  return obcx::command::catalog(
      obcx::command::observe<commands::PokeCommand>(
          "poke", "Poke the replied QQ user",
          obcx::command::re2(R"(^(?:poke|poke_user)$)")));
}
```

`name` remains required, must satisfy the existing canonical-name rules, and
continues to identify the registration, configuration edge, request message,
catalog entry, diagnostics, and processed headers. The matcher is an additional
way to select that registration; it is not a replacement identity.

The generated registration adds optional metadata:

```json
{
  "name": "poke",
  "description": "Poke the replied QQ user",
  "request_type": "bridge::commands::PokeCommand",
  "matcher": {
    "kind": "re2",
    "pattern": "^(?:poke|poke_user)$",
    "mode": "full"
  }
}
```

The nested shape makes the engine and match mode explicit and leaves room for
future matcher kinds without overloading the canonical name. Existing
observations emit no `matcher` member, so their generated contracts and exact
behavior remain unchanged. The contract field is additive in schema version 1;
the ABI generation and `obcx_get_actor_contract` symbol do not change.

Putting regexes in `command_runtime.routes` was rejected because the actor is
the owner of the command observation and request message. Repeating the matcher
in every bot scope would allow configuration to silently change a command's
accepted identity. Treating the pattern itself as `name` was rejected because
route keys, catalog APIs, diagnostics, and actor invocations require one stable
canonical string.

### 2. Match only the adapter's normalized command candidate

`ICommandPlatformAdapter` continues to validate platform syntax, command
boundaries, entities, and explicit bot targeting. On success it returns a
bounded normalized candidate name plus arguments. The matcher receives only
that candidate name.

The adapter MUST NOT apply actor patterns or select routes. The coordinator
MUST NOT run patterns over raw message text or JSON. Consequently an RE2 pattern
cannot bypass Telegram entity rules, accept a command targeted at another bot,
inspect content outside the command token, or make an actor platform-aware.

Adapters may return a platform-valid candidate that is not a valid canonical
OBCX name, such as a localized QQ alias. Canonical declarations remain subject
to the stricter existing name validation. Candidate normalization and length
limits remain adapter responsibilities.

RE2 matching uses `RE2::FullMatch` against the complete normalized candidate.
There is no implicit substring search. Authors who intentionally want a wider
form must express it in the pattern. Captures are ignored; arguments remain the
adapter-parsed argument string.

Matching raw text directly was rejected because the same regex would need to
reimplement slash boundaries, Telegram entities, `@bot` targeting, and
platform-specific escaping. Using `std::regex` was rejected because its syntax,
performance, and failure behavior do not provide RE2's bounded linear-time
matching guarantees.

### 3. Exact canonical names take precedence over patterns

For a normalized candidate on one `(platform, bot)` scope, the coordinator:

1. looks up an active exact canonical-name route;
2. if exact lookup succeeds, selects it without evaluating regex routes;
3. otherwise evaluates all active compiled patterns for that bot scope;
4. routes one unique match to its registration's existing request type;
5. treats zero matches as an ordinary unmatched command; and
6. treats more than one match as `command_match_ambiguous` without invoking an
   actor.

The invocation produced by a regex match contains the registration's canonical
name, not the alias text. Existing actor code, processed headers, completion
correlation, and catalog status therefore remain canonical. The source event
still contains the original platform data when business logic needs it.

Exact precedence makes every canonical command permanently addressable even if
another actor later adds an overlapping pattern. Declaration order,
configuration order, actor load order, and pattern iteration order never select
a winner. Adding numeric priority was rejected because it would hide overlap
behind deployment order and create another cross-actor ownership policy.

An ambiguous pre-transaction match records a bounded safe diagnostic and sends
the original `RawMessageEvent` through ordinary routing exactly once. No
route-specific fallback applies because no route or actor transaction was
selected.

### 4. Validate all declarations and precompile active generation state

The SDK validates matcher shape and compile-time constants that do not require
the RE2 runtime, including non-empty pattern text and the fixed matcher kind.
ActorManager validates parsed matcher structure, accepted fields, pattern byte
limits, and RE2 compilation before constructing or registering an actor.

Generation construction compiles each activated pattern into immutable
generation-owned matcher state using fixed RE2 options:

- UTF-8 encoding;
- `log_errors = false`;
- a bounded `max_mem`;
- a fixed maximum pattern length; and
- adapter-enforced maximum candidate length.

Startup, reload candidate preparation, and `--validate-config` use the same
validation path. Invalid syntax or resource limits reject the actor contract or
candidate before ingress and before external catalog publication. Identical
active patterns for the same bot scope are rejected as a static route conflict.
Different patterns can overlap in ways that cannot be generally proven during
validation, so actual multiple matches use the runtime ambiguity rule.

Compiling on every message was rejected due to avoidable allocation and
diagnostic variability. Allowing invalid patterns on inactive declarations was
rejected because the exported actor contract itself would be malformed and
could become active after a configuration-only reload.

### 5. Keep platform catalogs canonical

Aggregate catalog construction continues to use active canonical names and
descriptions. A command with a matcher contributes one entry under its ordinary
name. Pattern text and aliases are never sent to `setMyCommands` or another
platform catalog API.

This preserves platform naming constraints and makes remote menus a stable
subset of locally recognized forms. Selecting a canonical menu command still
uses the exact-match path.

### 6. Keep matchers generation-owned and diagnostics payload-free

Compiled RE2 objects live in the immutable command routing table owned by one
`RuntimeGeneration`. Candidate preparation cannot mutate active matchers, and
old-generation matchers remain alive through admitted command transactions
until drain completes. Superseded generation state is destroyed with its
routing table.

Safe diagnostics may include generation, platform, bot, canonical command,
matcher kind, failure code, and count of ambiguous matches. They MUST NOT log
the pattern, candidate token, arguments, raw message, or credentials. Stable
failure codes distinguish malformed metadata, invalid RE2, resource-limit
violations, duplicate active patterns, and runtime ambiguity.

## Risks / Trade-offs

- **[Two different RE2 patterns can overlap]** → Never choose by order; exact
  routes win, while multiple pattern matches produce
  `command_match_ambiguous` and ordinary routing once.
- **[A large pattern can consume excessive compile memory]** → Enforce fixed
  byte and RE2 memory limits before actor activation and reuse compiled
  generation state.
- **[Regex aliases are invisible in remote command menus]** → Publish only the
  canonical command; document patterns as local aliases rather than catalog
  entries.
- **[An adapter may return an unbounded candidate]** → Require adapter-specific
  candidate limits before coordinator matching.
- **[Pattern authors expect captures in actor arguments]** → Define matching as
  selection only; retain adapter-parsed arguments and defer capture protocols
  to a separate proposal.
- **[A new actor contract is loaded by an older runtime]** → Keep matcher
  metadata optional and additive; exact canonical routing remains the rollback
  behavior when matcher metadata is not activated.

## Migration Plan

1. Apply and archive `add-actor-command-routing` before this dependent change.
2. Add the optional SDK declaration/contract field and loader validation while
   preserving existing observation output.
3. Add immutable compiled matcher bindings and deterministic selection to the
   generation command table.
4. Update platform adapters to return bounded normalized candidates without
   applying actor matchers.
5. Add validation, ambiguity, reload, catalog, and sanitizer coverage.
6. Migrate individual commands by adding patterns only where aliases are
   required; existing configuration continues to activate canonical names.

Rollback removes regex alias recognition while canonical exact commands remain
available. Operators can first remove matcher declarations and reload actors,
then deploy the previous runtime; no persisted data or platform catalog
migration is required.

## Open Questions

None. Regex captures, platform-specific patterns, localized catalog aliases,
and explicit match priority require separate proposals.
