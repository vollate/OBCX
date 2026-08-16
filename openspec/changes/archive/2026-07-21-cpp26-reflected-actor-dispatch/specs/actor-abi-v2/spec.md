## ADDED Requirements

### Requirement: V2 actor library exports a generated input contract
Every V2 actor library SHALL export
`extern "C" const char *obcx_get_actor_contract()` through the V2 actor export
helper. The returned pointer MUST have static lifetime, and the JSON document
MUST contain a supported `schema_version`, the exported actor identity, and the
unique canonical `accepted_inputs` generated from the same reflected handler
set used by dispatch. The contract SHALL NOT declare actor outputs.

#### Scenario: Reflected actor exports its contract
- **WHEN** an actor library is built with the V2 export helper
- **THEN** the contract symbol returns deterministic JSON whose accepted inputs exactly match the actor's discovered handlers

#### Scenario: Actor author adds or removes a handler
- **WHEN** the actor's valid reflected handler set changes and the library is rebuilt
- **THEN** both generated dispatch and exported accepted inputs change from the same compile-time source

### Requirement: ActorManager validates input contracts before registration
ActorManager SHALL load and parse the required input contract before
constructing or registering an actor. It MUST reject a library whose symbol is
missing, whose pointer/document is invalid, whose schema version is
unsupported, whose actor identity disagrees with the exported actor name, or
whose accepted inputs are malformed or duplicated.

#### Scenario: Valid contract loads
- **WHEN** a V2 actor library reports the supported ABI generation and exposes a valid supported input contract
- **THEN** ActorManager stores the parsed contract with the loaded actor and may register the actor

#### Scenario: Old actor library lacks the contract
- **WHEN** a V2 actor library does not export `obcx_get_actor_contract`
- **THEN** ActorManager rejects it cleanly before actor construction or registration and reports the missing symbol

#### Scenario: Contract is malformed or unsupported
- **WHEN** the contract is invalid JSON or violates the supported contract schema
- **THEN** ActorManager rejects the library, releases its dynamic-library resources, and reports the contract error
