## ADDED Requirements

### Requirement: Explicit actor ABI generation
Every V2 actor library SHALL expose an explicit numeric ABI generation that is
independent from the actor's semantic version. ActorManager MUST determine the
generation before interpreting factory results or dispatching messages.

#### Scenario: Supported V2 generation loads
- **WHEN** an actor library reports the supported V2 ABI generation and provides all required V2 symbols
- **THEN** ActorManager constructs and registers it as an IActorV2

#### Scenario: Unsupported generation fails early
- **WHEN** an actor library reports an unsupported ABI generation
- **THEN** ActorManager rejects it during loading with an actionable error and never dispatches through an incompatible interface

### Requirement: V2 actor interface uses native ActorTask
The V2 actor interface SHALL return `ActorTask<ActorResult>` from
`handle_message` and SHALL NOT expose `boost::asio::awaitable` in its actor
dispatch contract. The SDK SHALL provide a V2 export helper and installed
headers required to build standalone V2 actors.

#### Scenario: Standalone V2 actor builds against installed SDK
- **WHEN** an external actor implements IActorV2 and uses the V2 export helper
- **THEN** it compiles, links, loads, and handles a message using only the installed OBCX actor SDK contract

#### Scenario: V2 actor suspends natively
- **WHEN** a V2 actor yields or awaits an ActorContext asynchronous operation
- **THEN** ActorScheduler retains ownership of its native ActorTask continuation

### Requirement: V1 and V2 actors coexist
ActorManager and ActorScheduler SHALL support V1 and V2 actor libraries in one
process during the compatibility window. Existing V1 factory symbols and
`IActor` behavior MUST remain loadable when V1 compatibility is enabled.

#### Scenario: Mixed-version pipeline
- **WHEN** a configured pipeline contains both a V1 actor and a V2 actor
- **THEN** the orchestrator routes messages and results across both actors with the same envelope and failure semantics

#### Scenario: V1 compatibility disabled
- **WHEN** runtime configuration disables V1 actors and a V1 library is discovered
- **THEN** ActorManager rejects it with a compatibility error before registration

### Requirement: V1 Asio actors adapt to native scheduling
The runtime SHALL provide a V1 adapter that starts the V1 actor's
`boost::asio::awaitable<ActorResult>` on an I/O executor and returns its
completion through the native actor scheduler. The adapter MUST preserve
mailbox exclusivity and MUST NOT run V1 actor state inline from an I/O callback.

#### Scenario: V1 actor awaits Asio
- **WHEN** a V1 actor suspends in its Asio awaitable
- **THEN** its native mailbox remains occupied without blocking an actor worker and completion returns through the scheduler boundary

#### Scenario: V1 actor throws
- **WHEN** a V1 actor awaitable exits with an exception
- **THEN** the adapter publishes the same actor failure semantics used for V2 exceptions exactly once

### Requirement: Public scheduling payload is renamed deliberately
The scheduler submission payload SHALL be named `ActorInvocation`, and
`ActorTask<T>` SHALL be reserved for the native coroutine type. Migration
documentation MUST identify the source-level rename for scheduler API users.

#### Scenario: Scheduler client migrates payload construction
- **WHEN** source code using the old non-coroutine ActorTask payload is rebuilt against V2 headers
- **THEN** documented replacement with ActorInvocation restores equivalent submission behavior

### Requirement: V1 removal requires a later compatibility decision
The change SHALL retain V1 loading and the `asio-v1` engine for an agreed
compatibility window. Removing either capability MUST occur in a separate
change after standalone actor repositories and rollback tests pass.

#### Scenario: V2 becomes default
- **WHEN** V2 passes rollout gates and becomes the default engine
- **THEN** operators can still select `asio-v1` and load supported V1 actors during the compatibility window
