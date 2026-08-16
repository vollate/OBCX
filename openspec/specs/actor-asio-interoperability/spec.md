# actor-asio-interoperability Specification

## Purpose
TBD - created by archiving change native-actor-coroutine-scheduler. Update Purpose after archive.
## Requirements
### Requirement: Asio callers submit through an initiating operation
ActorScheduler SHALL expose submission as an Asio-compatible asynchronous
initiating operation that accepts a completion token. Completion MUST be
delivered through the handler's associated executor and MUST preserve the
ActorResult or terminal scheduling error.

#### Scenario: Orchestrator awaits actor completion
- **WHEN** the Asio orchestrator submits an ActorInvocation with `use_awaitable`
- **THEN** it suspends until the native actor task completes and resumes on its associated Asio executor with the ActorResult

#### Scenario: Callback completion preserves executor affinity
- **WHEN** a caller submits with a callback bound to a specific Asio executor
- **THEN** the callback is dispatched on that associated executor rather than an actor worker

### Requirement: Actor tasks can await Asio operations
ActorContext SHALL provide an adapter that suspends ActorTask while an Asio
operation executes on an explicitly selected I/O executor. The suspended actor
task MUST NOT occupy an actor worker.

#### Scenario: Actor awaits delayed I/O
- **WHEN** an actor awaits an Asio timer or network operation that has not completed
- **THEN** its worker returns to scheduler work while the mailbox remains exclusively suspended

#### Scenario: Immediate Asio completion remains asynchronous at boundary
- **WHEN** the adapted Asio operation completes immediately
- **THEN** actor execution is still republished through ActorScheduler and is not resumed inline on the adapter's callback stack

### Requirement: Asio completion returns through ActorScheduler
An Asio completion callback SHALL store its value or exception and SHALL make
the actor continuation runnable through ActorScheduler. It MUST NOT directly
call `resume()` on the actor coroutine handle.

#### Scenario: Successful completion becomes stealable
- **WHEN** an awaited Asio operation completes successfully
- **THEN** its actor continuation is enqueued exactly once and may be resumed by any eligible actor worker

#### Scenario: Asio exception reaches actor code
- **WHEN** an adapted Asio coroutine completes with an exception
- **THEN** the corresponding actor await expression rethrows that exception when ActorScheduler next resumes the task

### Requirement: Boundary completion is exactly once
The actor-to-Asio adapter SHALL use shared operation state and an atomic
terminal transition so success, failure, cancellation, and late completion
cannot publish more than one actor continuation.

#### Scenario: Cancellation races with success
- **WHEN** cancellation and successful I/O completion occur concurrently
- **THEN** exactly one terminal transition wins and the actor task is either resumed with the result or terminated as cancelled, never both

#### Scenario: Late callback after task abandonment
- **WHEN** an underlying operation reports completion after its mailbox generation has been abandoned
- **THEN** the callback safely releases operation state and does not enqueue or resume the destroyed actor task

### Requirement: Cancellation propagates across the boundary
Actor cancellation SHALL request Asio cancellation when the underlying
operation supports it. Lack of underlying cancellation support MUST NOT permit
use-after-free or indefinite ownership loss.

#### Scenario: Cancellable operation acknowledges shutdown
- **WHEN** scheduler shutdown cancels an actor awaiting a cancellable Asio operation
- **THEN** the operation receives a cancellation request and the caller receives one terminal cancellation result

#### Scenario: Non-cancellable operation completes late
- **WHEN** cancellation is requested for an operation that cannot be interrupted
- **THEN** runtime-owned completion state remains valid until its callback arrives while the actor coroutine cannot be resumed after abandonment

### Requirement: Direct completion-token integration follows the safe adapter
The first V2 release SHALL provide the generic nested-coroutine Asio adapter.
A direct OBCX Asio completion token MAY replace it only after providing the same
executor, result, exception, cancellation, and exactly-once semantics.

#### Scenario: Direct token parity
- **WHEN** a supported Asio operation is migrated from the generic adapter to the OBCX completion token
- **THEN** the interoperability conformance tests produce identical observable behavior
