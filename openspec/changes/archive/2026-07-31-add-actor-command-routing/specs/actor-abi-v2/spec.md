## MODIFIED Requirements

### Requirement: V2 actor library exports a generated input contract

Every V2 actor library SHALL export
`extern "C" const char *obcx_get_actor_contract()` through the V2 actor export
helper. The returned pointer MUST have static lifetime, and the JSON document
MUST contain a supported `schema_version`, the exported actor identity, and the
unique canonical `accepted_inputs` generated from the same reflected handler
set used by dispatch. The contract MAY contain deterministic command
registrations generated from an actor's command declarations; each registration
MUST contain a canonical command name, description, and request type that is
also present in `accepted_inputs`. The contract SHALL NOT declare handler
names, callables, member-function pointers, platform implementations, completion
functions, or general actor outputs.

#### Scenario: Reflected actor exports its contract

- **WHEN** an actor library is built with the V2 export helper
- **THEN** the contract symbol returns deterministic JSON whose accepted inputs exactly match the actor's discovered handlers

#### Scenario: Actor exports command registrations

- **WHEN** an actor declares command observations for named reflected request message types
- **THEN** the same contract document contains deterministic command registrations mapping each command name to its description and canonical accepted request type without callable metadata

#### Scenario: Actor has no commands

- **WHEN** an actor does not declare command observations
- **THEN** its generated contract remains valid and represents an empty command-registration set

#### Scenario: Actor author adds or removes a handler

- **WHEN** the actor's valid reflected handler set changes and the library is rebuilt
- **THEN** both generated dispatch and exported accepted inputs change from the same compile-time source

#### Scenario: Actor author changes a command declaration

- **WHEN** an actor adds, removes, renames, or remaps a command declaration and the library is rebuilt
- **THEN** the exported command registrations change deterministically while reflected dispatch remains derived from typed handlers

### Requirement: ActorManager validates input contracts before registration

ActorManager SHALL load and parse the required input contract before
constructing or registering an actor. It MUST reject a library whose symbol is
missing, whose pointer/document is invalid, whose schema version is
unsupported, whose actor identity disagrees with the exported actor name, or
whose accepted inputs are malformed or duplicated. When command registrations
are present, ActorManager MUST also reject malformed or nondeterministically
ordered entries, duplicate command names, invalid canonical names, empty
descriptions, and request types absent from the same contract's accepted input
set.

#### Scenario: Valid contract loads

- **WHEN** a V2 actor library reports the supported ABI generation and exposes a valid supported input contract
- **THEN** ActorManager stores the parsed contract with the loaded actor and may register the actor

#### Scenario: Valid command registrations load

- **WHEN** a contract contains sorted unique command registrations whose request types are accepted inputs
- **THEN** ActorManager stores those registrations with the actor contract for later generation validation

#### Scenario: Command request type is not accepted

- **WHEN** a command registration names a request type absent from the contract's accepted inputs
- **THEN** ActorManager rejects the library before actor construction or registration

#### Scenario: Command registration contains callable metadata

- **WHEN** a command registration contains a handler, function, callable, or member-pointer field
- **THEN** ActorManager rejects the unsupported member instead of treating it as dispatch metadata

#### Scenario: Old actor library lacks the contract

- **WHEN** a V2 actor library does not export `obcx_get_actor_contract`
- **THEN** ActorManager rejects it cleanly before actor construction or registration and reports the missing symbol

#### Scenario: Contract is malformed or unsupported

- **WHEN** the contract is invalid JSON or violates the supported contract schema
- **THEN** ActorManager rejects the library, releases its dynamic-library resources, and reports the contract error
