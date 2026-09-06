## MODIFIED Requirements

### Requirement: Bridge performs bot calls only through the operation client
Bridge forwarding, mutation, bot-owned media calls, OneBot lookups, poke, command replies, and retry sends SHALL use common, Telegram, or OneBot typed SDK adapters backed by the shared `BotOperationGateway`. Every call MUST retain the exact installation and conversation selected by its source route or persisted retry. Bridge MUST NOT obtain process component catalogs, handler registrations, `BotRegistry`, live/provider bot interfaces, connection managers, provider executors, or raw send/delete/edit response envelopes, and MUST NOT replace a missing target with another pair. Platform-specific types SHALL be included from their owning platform SDK rather than an all-platform client.

#### Scenario: Direct forwarding sends a message
- **WHEN** Bridge forwards a current QQ or Telegram message from one configured pair
- **THEN** the common group-send or Telegram topic adapter invokes the same exact target installation and the actor consumes a validated typed message reference

#### Scenario: Selected installation is unavailable
- **WHEN** that exact installation cannot execute the requested operation
- **THEN** Bridge returns the typed failure without calling another installation of the same platform or another pair

#### Scenario: Architecture dependency is scanned
- **WHEN** Bridge production dependencies are inspected and compiled against the installed SDK
- **THEN** only public/common and selected platform contracts plus the gateway are needed for Bot egress, with no all-platform virtual client or process provider dependency

#### Scenario: Gateway reports uncertain delivery
- **WHEN** a side-effecting call returns a possibly-submitted result or an invalid SDK success payload
- **THEN** Bridge creates no fabricated mapping or automatic resend and preserves its current failure policy

#### Scenario: Current multi-pair and conversation state is reused
- **WHEN** rebuilt Bridge runs against its existing schema-3 database and valid scalar or multi-pair configuration
- **THEN** direct-send ownership, deduplication, exact conversation references, media/command behavior, persisted retry targets, and reload semantics remain unchanged without a new schema migration
