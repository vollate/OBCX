## MODIFIED Requirements

### Requirement: Platform adapters only translate platform command semantics
The command runtime SHALL consume generic `ICommandPlatformAdapter` bindings supplied by the registered platform module for each exact configured installation. Platform modules SHALL own command detection/normalization, non-secret bot-target metadata, catalog validation, and optional process-only publication implementations. Generic coordinator, generation builder, and installation directory MUST NOT depend on concrete platform configuration, a `TelegramCommandCatalog` parameter, a hardcoded platform factory, or an else-to-OneBot fallback. An adapter MUST NOT select actors, invoke handlers, retain transactions, or decide source-event propagation.

#### Scenario: Telegram command is normalized
- **WHEN** a Telegram raw event contains a platform-valid command entity targeted at the receiving bot
- **THEN** the module-provided adapter uses its validated bot-target metadata to return the canonical name, arguments, and source context without invoking an actor

#### Scenario: Platform-specific target does not match
- **WHEN** a command explicitly targets another bot identity
- **THEN** the adapter reports no command and ordinary routing remains available

#### Scenario: Adapter does not support catalog publication
- **WHEN** an installation supports command detection but has no remote catalog publisher
- **THEN** local command routing remains available and publication is a typed unsupported/no-op with no provider call

#### Scenario: Module binding is unavailable
- **WHEN** an active route targets an installation whose registered module has no required command adapter
- **THEN** generation validation rejects that route without inferring a Telegram or OneBot adapter

### Requirement: Platform catalogs aggregate active registrations
For each installation whose module provides a command-catalog publisher, OBCX SHALL derive one deterministic aggregate from all active scoped command routes and invoke a generic process-only publisher bound to that exact installation. It MUST publish the complete aggregate rather than allow individual actors to replace platform state. Reconciliation SHALL begin only after startup activation or successful generation cutover. Publication failure MUST leave local routing active, expose degraded status, and follow existing bounded retry without rolling back to a partially active generation. No provider-specific publisher type SHALL appear in the generic coordinator API.

#### Scenario: Multiple actors contribute Telegram commands
- **WHEN** two actors have distinct active commands for one Telegram installation
- **THEN** its publisher receives one sorted aggregate containing both registrations

#### Scenario: Candidate preparation succeeds before cutover
- **WHEN** a candidate command table is valid but has not become active
- **THEN** no platform command-menu mutation occurs

#### Scenario: Remote catalog update fails
- **WHEN** a publisher rejects or times out an aggregate update after activation
- **THEN** local routing remains active and diagnostics expose the desired catalog, outcome, and retry state without credentials

#### Scenario: Several same-platform installations publish catalogs
- **WHEN** different Telegram installations have different active routes
- **THEN** each aggregate is published through its own bound capability without cross-installation lookup or platform-only fallback
