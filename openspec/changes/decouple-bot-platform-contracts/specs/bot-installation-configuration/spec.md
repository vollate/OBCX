## MODIFIED Requirements

### Requirement: Bot configuration identifies exact installations and surfaces
Each `bots.<installation_id>` table SHALL explicitly define `enabled`, `surface`, and `transport`. The table key SHALL remain the installation id used by ingress and the gateway. Shared parsing SHALL validate common table structure, then resolve the exact registered surface/transport recipe from an injected immutable platform catalog. Current production surfaces SHALL remain exactly `onebot11.qq` and `telegram.bot_api`; no default or alias selection is permitted.

#### Scenario: Valid OneBot installation is parsed
- **WHEN** configuration declares `onebot11.qq` with `websocket` or `http`
- **THEN** the OneBot module parser produces the matching typed plan with the table key as installation id

#### Scenario: Valid Telegram installation is parsed
- **WHEN** configuration declares `telegram.bot_api` with `http`
- **THEN** the Telegram module parser produces its typed HTTP plan with the table key as installation id

#### Scenario: Unsupported surface is configured
- **WHEN** production configuration declares `qq.official`, an unregistered surface, or a platform alias
- **THEN** validation rejects that path before transport construction or network activity

### Requirement: Configuration produces a closed typed connection variant
For each registered recipe, its owning module SHALL parse the connection table once into a validated immutable typed configuration plan. The plan SHALL expose generic public metadata, a static recipe/operation description, a secret-safe fingerprint contribution, and process-only assembly behavior. Concrete connection types SHALL remain private to the module; the generic config loader and installed common SDK MUST NOT define a cross-platform connection variant. Only the module parser SHALL interpret raw connection TOML; runtime assembly/components MUST NOT reparse it or select a provider fallback.

#### Scenario: OneBot WebSocket configuration reaches assembly
- **WHEN** the OneBot parser accepts a WebSocket configuration
- **THEN** its typed plan constructs that recipe without a generic-core `std::get` or a possible Telegram/HTTP reinterpretation

#### Scenario: Unknown transport is configured
- **WHEN** transport is missing or has no registration for the exact surface
- **THEN** parsing fails instead of selecting a default enum or recipe

#### Scenario: Metadata is consumed by an actor
- **WHEN** an actor or actor validation path reads configured installation metadata through the public SDK
- **THEN** it receives explicit identity, surface, enabled status, and permitted non-secret metadata but no provider connection type, token, proxy credentials, or assembly callback

#### Scenario: Independent actor test builds configuration context
- **WHEN** a standalone SDK consumer constructs an actor-only configuration context from explicit actor-owned values and non-secret installation metadata
- **THEN** the public SDK provides that data-only view without a platform catalog, connection parser, provider startup, or full process-configuration validation claim; full loader/catalog APIs remain process-private and no separate process-config SDK is published

#### Scenario: Actor-only builder receives a bot connection table
- **WHEN** input to the actor-only builder contains a private Bot connection table instead of explicit installation metadata
- **THEN** construction rejects it rather than preserving credentials or silently treating it as validated process configuration

### Requirement: Connection keys and units are explicit and validated
Each module parser SHALL maintain a closed key set for each supported recipe, require every currently mandatory option explicitly, use `_ms` suffixes for durations, and preserve existing port, timeout, TLS/proxy, credential, and polling validation. Unknown, misspelled, ignored, misplaced, legacy, or missing mandatory keys MUST produce path-specific secret-safe diagnostics. Optional proxy configuration can remain absent as a whole; when supplied, all fields required by the current explicit schema MUST be present. No implicit provider, transport, credential, TLS, timeout, or proxy-option defaults SHALL be introduced.

#### Scenario: Legacy timeout key is present
- **WHEN** a connection contains `timeout` instead of a supported explicit `_ms` field
- **THEN** the owning parser rejects it and identifies the full configuration path

#### Scenario: Provider-specific field is misplaced
- **WHEN** an OneBot WebSocket connection contains a Telegram polling option
- **THEN** parsing rejects it instead of ignoring it

#### Scenario: Mandatory option is omitted
- **WHEN** Telegram configuration omits `action_timeout_ms` or a configured proxy omits its required username/password field
- **THEN** parsing fails and does not supply an implicit value

#### Scenario: Disabled bot is malformed
- **WHEN** a disabled installation contains unknown keys or lacks a mandatory connection field
- **THEN** the same module schema rejects it rather than deferring validation until it is enabled

### Requirement: Validation-only and reload are side-effect free and component aware
Bot validation-only preparation SHALL parse typed module plans, validate recipe DAGs and static operation descriptions, and check actor installation references without constructing transports, starting bot workers, publishing catalogs, or calling providers. It SHALL use the same injected catalog as startup and reload. Fingerprints SHALL include enabled state, exact identity, surface, transport, all validated connection options including credential changes via digests, and existing process thread-budget inputs. Actor reload SHALL reuse active installations and reject process configuration or recipe/manifest changes as restart-required.

#### Scenario: Validation-only checks a valid configuration
- **WHEN** validation receives a current canonical bot configuration
- **THEN** plans, recipes, operations, and actor references are checked without provider or operation-persistence side effects

#### Scenario: Reload changes an active installation recipe
- **WHEN** a candidate changes surface, transport, connection options, or process recipe ownership
- **THEN** reload reports restart-required and leaves the active installation unchanged

#### Scenario: Reload references a missing capability
- **WHEN** a candidate requires an operation absent from its exact active installation
- **THEN** candidate validation fails before activation

#### Scenario: Secret or disabled-bot configuration changes
- **WHEN** only a token or a disabled installation's connection value changes
- **THEN** the process fingerprint changes without exposing that value in public metadata, logs, or diagnostics

#### Scenario: Actor-only reload is valid
- **WHEN** actor routes change while bot plans and process budget remain identical
- **THEN** candidate preparation reuses the same catalog, gateway, and active installations
