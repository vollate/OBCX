## Why

Local actors currently detect slash commands inside ordinary platform message
handlers, which duplicates Telegram/QQ parsing, lets actors overwrite one
another's published command menus, and provides no runtime-level way for a
handled command to continue or consume the original message. OBCX needs one
actor-native command protocol that keeps platform syntax in adapters and keeps
command execution as typed message exchange between actors.

## What Changes

- Add a generation-owned command coordinator that observes root
  `RawMessageEvent` ingress, asks the selected platform adapter to normalize a
  command, sends the actor-declared request message to the configured actor, and
  consumes or resumes the original event after receiving a correlated
  `CommandCompleted` message.
- Add a generic `ICommandPlatformAdapter` capability with per-platform
  implementations for command detection and optional aggregate command-catalog
  publication. Platform adapters do not select actors, invoke actor functions,
  or own command transactions.
- Let an actor declare command observations as command name, description, and
  typed request message. The declaration exposes no member-function pointer;
  the request is delivered through the existing reflected actor message
  contract and completion is returned as an ordinary actor message.
- Extend the generated actor input contract with optional deterministic command
  registrations and validate that every registered request type is one of that
  actor's reflected accepted inputs.
- Add explicit configuration that activates actor-declared commands for
  platform and bot scopes. Candidate generations reject missing actors,
  undeclared commands, unsupported request types, incompatible adapters, and
  conflicting active registrations before accepting ingress.
- Define correlated command transactions, exactly-one completion, actor-selected
  `continue`/`consume` propagation, configured failure/timeout fallback, and a
  processed marker that prevents a continued source event from being detected
  twice.
- Aggregate only active registrations per bot for platform menu publication.
  Candidate preparation performs no external mutation; catalog reconciliation
  begins after generation activation and reports/retries platform failures
  without disabling command routing.
- **BREAKING**: Actors that currently parse commands from
  `RawMessageEvent` must migrate those commands to typed request messages and
  require matching command-route configuration. Non-command raw-message
  behavior remains on the ordinary pipeline.

## Capabilities

### New Capabilities

- `actor-command-routing`: Platform-neutral command observation, explicit
  actor/config registration, typed request and completion messages, original
  message propagation, generation lifecycle, catalog publication, and
  diagnostics.

### Modified Capabilities

- `actor-abi-v2`: Extend the generated actor input contract with optional
  command registrations whose request types must be reflected accepted inputs,
  without declaring handlers, callables, or general actor outputs.

## Impact

- Public actor SDK command message/contract helpers and the generated
  `obcx_get_actor_contract` JSON document.
- Actor contract parsing, runtime configuration, generation building, root
  ingress orchestration, routing diagnostics, reload drain accounting, and
  validation-only startup.
- Platform bot adapters, including Telegram aggregate command-menu
  reconciliation and platform implementations that support detection only.
- Command-owning local actors such as `chat_llm` and `obcx-actor-bridge`, plus
  actor templates and integration/sanitizer test coverage.
