## MODIFIED Requirements

### Requirement: A bot installation is a composition root
The process SHALL represent each enabled configured bot as one installation-scoped runtime owning its identity, executor, component instances, capability registry, and lifecycle. The installation MUST NOT inherit provider, protocol, transport, messaging, media, moderation, command, or operation capability interfaces. Its generic assembler SHALL consume a validated module-owned recipe/plan without enumerating concrete platform components or inspecting platform configuration variants.

#### Scenario: OneBot installation is assembled
- **WHEN** a valid `onebot11.qq` plan is created by the OneBot module
- **THEN** its selected WebSocket or HTTP recipe assembles protocol, transport, ingress, and operation components without constructing a god object or invoking a generic-core platform switch

#### Scenario: Telegram installation is assembled
- **WHEN** a valid `telegram.bot_api` plan is created by the Telegram module
- **THEN** its HTTP recipe assembles protocol, transport, ingress, uploader, operation, and command-catalog components under one installation owner

### Requirement: Process-only platform features are capabilities
Process command-catalog and event consumers SHALL use explicit generic installation capability contracts and module-supplied metadata. The generic installation directory, command coordinator, and generation builder MUST NOT include Telegram/OneBot config or provider interfaces or infer a surface from an else branch. Platform-specific command detection and catalog implementation SHALL belong to the platform module. Absence of an optional publisher SHALL return typed unsupported/no-op status before provider I/O.

#### Scenario: Telegram command catalog is published
- **WHEN** reconciliation targets a Telegram installation with a command-catalog publisher
- **THEN** the process invokes the generic publisher capability implemented by that module without resolving a `TelegramCommandCatalog` type in generic runtime code

#### Scenario: Command catalog targets OneBot
- **WHEN** reconciliation targets an OneBot installation without a remote catalog capability
- **THEN** no provider publication occurs and local command routing remains usable

#### Scenario: Ingress carries platform metadata
- **WHEN** a platform module publishes a valid event
- **THEN** generic ingress uses the module's explicit surface/platform metadata and owning installation id, preserving existing `qq` and `telegram` envelope semantics

### Requirement: Legacy bot inheritance infrastructure is removed
The runtime and installed SDK SHALL NOT define or depend on oversized `IBot`, provider bot interfaces, `QQBot`/`TGBot`, `BotRegistry`, or the former all-platform `BotOperationClient`. Production component, dispatcher, command, and ingress code MUST contain no live-bot cast path. Actor bot calls SHALL use the common `BotOperationGateway` through common or platform-owned typed contracts without exposing process component headers.

#### Scenario: Architecture boundary is checked
- **WHEN** production runtime and installed actor interfaces are scanned and compiled
- **THEN** removed live-bot interfaces and all-platform operation overloads are absent from the public dependency graph

#### Scenario: Actor package sends a bot operation
- **WHEN** an installed actor package needs a supported QQ or Telegram action
- **THEN** it compiles against common gateway headers and only the platform contracts it uses, without including process component headers

## ADDED Requirements

### Requirement: Optional operation dependencies participate in the lifecycle graph
A platform recipe SHALL construct component descriptors that represent every dependency used by its selected executable operations. When Telegram upload is installed, its operations component SHALL depend on that installation's uploader in the lifecycle graph and register upload only after successful preparation. Omitting upload in an isolated test recipe SHALL remove both the dependency and operation advertisement, without resolving an uploader from another installation.

#### Scenario: Upload-enabled recipe starts and stops
- **WHEN** a Telegram recipe includes multipart upload
- **THEN** uploader preparation precedes dependent operation preparation and reverse lifecycle ordering retires its consumers before its dependencies

#### Scenario: Upload-disabled test recipe is assembled
- **WHEN** a test recipe intentionally omits the uploader
- **THEN** non-upload Telegram operations remain available and upload fails as unsupported before I/O

#### Scenario: Selected upload dependency is missing
- **WHEN** operations declare upload support but the recipe lacks their uploader dependency
- **THEN** assembly fails rather than publishing a non-executable upload action
