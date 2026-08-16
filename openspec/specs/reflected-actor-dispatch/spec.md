# reflected-actor-dispatch Specification

## Purpose
TBD - created by archiving change cpp26-reflected-actor-dispatch. Update Purpose after archive.
## Requirements
### Requirement: Actor SDK requires admitted C++26 reflection
The core actor targets and installed actor SDK SHALL require C++26 static
reflection with `__cpp_impl_reflection >= 202506L`. The supported build SHALL
use an admitted GCC 16 toolchain with C++26 and reflection enabled, and SHALL
NOT provide a C++20 registration fallback.

#### Scenario: Admitted compiler configures the SDK
- **WHEN** GCC 16 satisfies the required reflection feature probe
- **THEN** core and standalone actor targets configure in C++26 mode with static reflection enabled

#### Scenario: Compiler lacks required reflection
- **WHEN** a compiler does not satisfy the required reflection feature probe
- **THEN** configuration fails with an actionable compiler and reflection requirement diagnostic

### Requirement: Reflected actor discovers typed handlers without registration
The SDK SHALL provide `ReflectedActor<Derived>` as an `IActorV2` authoring base
whose final `handle_message` implementation discovers public direct `handle`
overloads. Every discovered handler MUST have the exact logical signature
`(const Message&, const MessageEnvelope&, ActorContext&)` and MUST return either
`ActorResult` or `ActorTask<ActorResult>`.

#### Scenario: Actor declares sync and async overloads
- **WHEN** an actor declares valid public direct `handle` overloads with both supported return forms
- **THEN** the actor compiles and each overload is included in generated dispatch without explicit registration or a manual `handle_message`

#### Scenario: Handler signature is malformed
- **WHEN** a `handle` candidate has invalid visibility, parameter cv-reference shape, parameter order, arity, or return type
- **THEN** actor compilation fails with a diagnostic identifying the invalid handler property

#### Scenario: Actor has no valid handler
- **WHEN** a reflected actor declares no valid public direct `handle` overload
- **THEN** actor compilation fails instead of exporting an empty dispatcher

#### Scenario: Normalized inputs are duplicated
- **WHEN** two handlers normalize through alias removal to the same message type
- **THEN** actor compilation fails with a duplicate-input diagnostic

### Requirement: Message identity is the fully qualified reflected type name
The SDK SHALL derive a message's canonical wire identity by removing aliases,
walking its reflected named parents, reading each component with
`identifier_of`, and joining them with `::` without a leading separator. It
MUST NOT use `display_string_of` as canonical identity and MUST NOT provide
short-name or legacy aliases.

#### Scenario: Namespaced message identity is generated
- **WHEN** a handler accepts `obcx::message_store::events::MessageStored`
- **THEN** receive dispatch, typed emit, and the actor input contract use exactly `obcx::message_store::events::MessageStored`

#### Scenario: Message type is not stably nameable
- **WHEN** a handler message is local, anonymous, lambda-generated, or enclosed by an unnamed contributing scope
- **THEN** actor compilation fails with a canonical-identity diagnostic

#### Scenario: Message type moves or is renamed
- **WHEN** a message's named namespace or type identifier changes
- **THEN** its generated wire identity changes and no old identity is accepted implicitly

### Requirement: Generated dispatch decodes and invokes the exact handler
For an accepted canonical input, generated dispatch SHALL deserialize
`MessageEnvelope.payload` into the exact normalized handler message type using
nlohmann JSON ADL conversion and SHALL invoke the exact reflected overload.
Synchronous `ActorResult` returns SHALL be normalized to the native
`ActorTask<ActorResult>` dispatch boundary.

#### Scenario: Supported message reaches its overload
- **WHEN** an envelope type equals a generated canonical input and its payload is valid JSON for that message
- **THEN** the exact corresponding overload receives the decoded value, original envelope, and actor context

#### Scenario: Message type is unsupported
- **WHEN** an envelope type is not in the actor's generated input set
- **THEN** dispatch returns an `unsupported_message_type` failure without calling a handler

#### Scenario: Payload cannot be decoded
- **WHEN** an accepted envelope contains JSON that cannot be converted to its handler message type
- **THEN** dispatch returns an `invalid_message_payload` failure without logging the complete payload

#### Scenario: JSON conversion is unavailable
- **WHEN** a handler input lacks the required nlohmann JSON conversion
- **THEN** actor compilation fails with a conversion diagnostic

### Requirement: Async dispatch retains decoded input lifetime
Generated dispatch SHALL retain the decoded message and required dispatch state
until the root `ActorTask<ActorResult>` reaches terminal completion. JSON decode
and handler-task construction MUST execute as scheduler-owned actor work and
MUST NOT execute while scheduler or mailbox synchronization locks are held.

#### Scenario: Handler suspends while holding its input reference
- **WHEN** an async handler suspends and later reads its `const Message&` parameter after resumption
- **THEN** the decoded message remains alive and unchanged until the handler completes

#### Scenario: Invocation is created before worker claim
- **WHEN** the scheduler accepts an invocation before an actor worker first resumes it
- **THEN** generated user dispatch and JSON decoding do not run on the submitting thread under scheduler synchronization

### Requirement: Typed emit derives identity and preserves routing metadata
`ActorResult` SHALL provide a typed emit operation that serializes a message
through nlohmann JSON, derives its canonical reflected identity, and inherits
the required correlation and routing metadata from a parent envelope. The
low-level `MessageEnvelope` emit operation SHALL remain available for
infrastructure and dynamic adapters, and `ActorResult` SHALL remain
non-template.

#### Scenario: Actor emits a typed message
- **WHEN** a handler emits a named message with a parent envelope
- **THEN** the result contains an envelope with the generated full type name, serialized payload, and inherited routing metadata

#### Scenario: Infrastructure emits a complete envelope
- **WHEN** infrastructure uses the low-level envelope operation
- **THEN** the supplied envelope is retained without requiring a reflected output declaration

#### Scenario: Output JSON conversion is unavailable
- **WHEN** actor code instantiates typed emit for a message lacking nlohmann JSON serialization
- **THEN** actor compilation fails with a conversion diagnostic

### Requirement: Reflected dispatch remains local to its actor generation

Generated reflected dispatch SHALL resolve and invoke handlers using code and
actor-specific metadata owned by the currently executing actor library
generation. It MUST NOT retain a process-unique or cross-generation static
dispatch table containing canonical-name views, actor dispatch function
pointers, or other references whose lifetime belongs to a separately staged
copy of the actor DSO.

#### Scenario: The same actor is reloaded after dispatch was initialized

- **WHEN** one staged actor generation handles a supported canonical message and later reloads stage new copies of the same actor DSO
- **THEN** every new generation recognizes that canonical message and invokes the handler compiled into its own loaded image

#### Scenario: A retired generation is released

- **WHEN** the last route using a retired actor generation completes and its DSO becomes eligible for unload
- **THEN** no reflected dispatch state used by an active generation references the retired generation's canonical-name views or handler functions

#### Scenario: Current generation receives an unsupported type

- **WHEN** the active actor generation receives a canonical input that none of its reflected handlers accept
- **THEN** dispatch returns `unsupported_message_type` without consulting metadata from any earlier generation
