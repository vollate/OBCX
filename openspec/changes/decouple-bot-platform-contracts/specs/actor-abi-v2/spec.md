## MODIFIED Requirements

### Requirement: V2 actor library exports a generated input contract
Every V2 actor library SHALL export `extern "C" const char *obcx_get_actor_contract()` through the V2 export helper. Its pointer MUST have static lifetime. Actors built with the modular Bot SDK SHALL emit `schema_version = 2` as an explicit SDK contract compatibility boundary while retaining numeric actor dispatch ABI generation 2. The document MUST contain actor identity and sorted unique canonical `accepted_inputs` derived from reflected handlers. Command entries, when present, MUST retain canonical name, description, and an accepted request type.

The document SHALL preserve existing deterministic scalar constraints, `bot_installation_collections`, and `collection_identity_references`. Collections MUST retain minimum cardinality, item identity, required bot fields, and scalar/collection alternative groups; identity references MUST retain their declarative scalar/root-array relationships. Bot field expectations SHALL name exact surface IDs rather than legacy `qq`, `onebot`, or `telegram` aliases. These constraints MUST contain data only, not callable validators, live bots, provider objects, credentials, connections, executors, handler names, completion functions, or general outputs. Shared schema parsing MUST NOT enumerate platform support; supported exact surfaces SHALL be resolved during generation validation using the injected module catalog.

#### Scenario: Reflected actor exports its contract
- **WHEN** an actor is built with the new V2 export helper
- **THEN** it emits schema 2 with accepted inputs matching its handlers and retains deterministic command/configuration declarations

#### Scenario: Actor exports command registrations
- **WHEN** an actor declares typed commands
- **THEN** schema 2 preserves the name/description/request-type mapping and requires each request type in accepted inputs

#### Scenario: Actor exports repeated bot references
- **WHEN** an actor declares named installation pairs
- **THEN** the generated schema 2 constraints retain field names, identities, cardinality, alternatives, references, and exact expected surfaces without runtime bot values

#### Scenario: Actor has no commands or configuration constraints
- **WHEN** an actor declares neither optional command nor configuration metadata
- **THEN** schema 2 remains valid with empty optional sets

#### Scenario: Actor author changes a handler or command
- **WHEN** reflected handlers or command declarations change
- **THEN** contract generation and dispatch remain derived from the same declarations and produce deterministic updated metadata

#### Scenario: Actor author changes collection constraints
- **WHEN** fields, expected surfaces, cardinality, identities, or alternatives change
- **THEN** the contract reflects those changes without executable metadata or core platform enum edits

### Requirement: ActorManager validates input contracts before registration
ActorManager SHALL parse the required input contract before factory invocation, generation preparation, service use, or route registration. The modular SDK runtime MUST accept schema 2 only and reject schema 1 or unknown schema versions with an actionable rebuild diagnostic, even if the library reports dispatch ABI generation 2. It MUST retain checks for missing symbols, invalid pointers/documents, mismatched actor identity, malformed or duplicate inputs, and malformed/nondeterministic command entries or request types absent from inputs.

Configuration parsing MUST retain checks for unknown members, malformed scalar/collection/reference constraints, duplicate names, empty fields or expected surface sets, invalid cardinality, unresolved identity references, and inconsistent alternative declarations. Exact surface ID syntax SHALL be validated without a hardcoded supported-platform list; unregistered surfaces and incorrect configured installations MUST fail generation validation against the module catalog before activation. Parsing MUST NOT inspect credentials or start providers.

#### Scenario: Valid contract loads
- **WHEN** a rebuilt library reports supported dispatch ABI 2 and valid schema 2
- **THEN** ActorManager stores its validated input, command, and configuration contracts before construction/registration

#### Scenario: Old SDK actor is offered
- **WHEN** a previously built ABI 2 actor emits schema 1
- **THEN** startup, validation-only, and reload reject it before its factory or preparation hook runs and identify the required SDK rebuild

#### Scenario: New SDK actor is offered to an old runtime
- **WHEN** a schema 2 actor is inspected by a runtime accepting schema 1 only
- **THEN** the incompatible schema is rejected rather than interpreting new service/value layouts as the old SDK

#### Scenario: Valid command and installation constraints load
- **WHEN** schema 2 contains sorted commands and valid finite installation collection constraints
- **THEN** the loader retains those declarations for unchanged generation-level route and pair validation

#### Scenario: Command request type is not accepted
- **WHEN** a command entry names a type absent from accepted inputs
- **THEN** the library is rejected before construction

#### Scenario: Contract contains callable metadata
- **WHEN** command or configuration metadata contains a handler, function, validator symbol, credential, or provider object
- **THEN** the loader rejects the unsupported member

#### Scenario: Collection constraint is malformed
- **WHEN** a collection lacks identity, repeats a field, has an empty expected-surface set, or declares inconsistent alternatives
- **THEN** the loader rejects it before construction

#### Scenario: Exact surface is not registered
- **WHEN** structurally valid schema 2 constraints require a surface missing from the application's catalog
- **THEN** generation validation fails without a platform alias, provider startup, or route activation

#### Scenario: Old library lacks a contract
- **WHEN** an actor has no required contract symbol
- **THEN** the loader rejects it cleanly and reports the missing contract

#### Scenario: Contract is malformed or unsupported
- **WHEN** JSON or schema contents are invalid or the schema version is unsupported
- **THEN** the loader releases its resources and rejects the actor before interpreting its service contract

### Requirement: V2 actors may prepare a generation before route activation
The V2 export helper SHALL retain the additive generation-preparation entry point. Actors can implement typed `prepare_generation(ActorContext&)`; actors without a hook or preparation symbol SHALL be treated as ready only after passing the supported dispatch ABI and schema 2 SDK compatibility gate. The runtime MUST invoke preparation after validated configuration and generation services become available but before scheduler, command, or pipeline registration. Preparation MUST return ready, failed, or restart-required without exposing callables in the input contract; absence of this optional symbol MUST NOT permit an incompatible schema 1 actor to load.

#### Scenario: Actor prepares startup-owned state
- **WHEN** a compatible actor has a generation-preparation hook
- **THEN** it receives exact actor id, database scope, configuration, generation purpose, and services before any route can invoke it

#### Scenario: Preparation fails
- **WHEN** a compatible actor reports failure
- **THEN** generation construction fails with a bounded diagnostic and no routes are published

#### Scenario: Preparation requires process restart
- **WHEN** a compatible reload candidate requires a startup-only state migration
- **THEN** the runtime returns restart-required and leaves the active generation authoritative

#### Scenario: Compatible V2 actor has no preparation export
- **WHEN** an otherwise valid schema 2 actor lacks the optional preparation symbol
- **THEN** construction and dispatch remain supported and preparation is treated as ready

#### Scenario: Validation-only preparation runs
- **WHEN** a compatible actor is prepared for validation-only mode
- **THEN** it receives the validation purpose and checks configuration without starting ingress or mutating actor-owned state
