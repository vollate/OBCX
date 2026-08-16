## ADDED Requirements

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
