# actor-blocking-execution Specification

## Purpose
TBD - created by archiving change replace-bot-heavy-task-api. Update Purpose after archive.
## Requirements
### Requirement: Actor blocking execution is independent of bots

The runtime SHALL expose blocking execution through
`ActorContext::run_blocking()` and a runtime-registered `BlockingExecutor`.
Submitting blocking work MUST NOT require, inspect, or retain an `IBot`
instance. Production and installed SDK artifacts MUST NOT expose
`TaskScheduler`, `IBot::get_task_scheduler()`,
`IBot::run_heavy_task()`, or equivalent bot-owned access to a blocking pool.

#### Scenario: Actor submits blocking work without a bot

- **WHEN** an actor with a valid runtime context awaits a synchronous callable through `run_blocking`
- **THEN** the callable is accepted through the runtime `BlockingExecutor` without resolving or capturing a bot

#### Scenario: Retired bot scheduling API is audited

- **WHEN** public headers, bot constructors, production sources, and installed SDK artifacts are inspected
- **THEN** no bot-owned task scheduler, heavy-task helper, compatibility forwarding method, or `TaskScheduler` header remains

#### Scenario: Nested Asio actor code submits blocking work

- **WHEN** generation-tracked code already executing inside an `ActorContext::await_asio` coroutine needs a synchronous database, filesystem, HTTP, or CPU call
- **THEN** it can await the registered `BlockingExecutor` operation without using a bot or exposing the underlying thread pool

### Requirement: Blocking callables execute only on blocking workers

An admitted blocking callable SHALL execute on the fixed-size process blocking
executor selected by the runtime thread budget. The actor or Asio caller SHALL
suspend while the callable runs and MUST NOT occupy an actor worker or execute
the callable on the caller's I/O executor. Callables MUST be synchronous and
MUST NOT return references or awaitables.

#### Scenario: Actor worker is released

- **WHEN** an actor awaits a blocking callable that is held by a deterministic test gate
- **THEN** its actor worker is available to execute runnable work for another mailbox while the callable runs on a blocking worker

#### Scenario: Nested Asio caller is released

- **WHEN** an Asio coroutine awaits a blocking callable
- **THEN** its I/O executor remains able to run unrelated handlers while the callable runs on a blocking worker

#### Scenario: Invalid callable result is rejected

- **WHEN** actor code attempts to submit a callable returning a reference or another awaitable
- **THEN** the SDK rejects that use at compile time instead of extending the referenced lifetime or nesting asynchronous work in the blocking pool

### Requirement: Blocking completion is event-driven and executor-safe

The blocking executor SHALL publish completion through an Asio initiating
operation without polling a future or timer. It SHALL preserve void and value
completion, move-only callable and result state, and exceptions. Completion for
an Asio caller MUST be dispatched exactly once through the completion handler's
associated executor, including immediate completion and submission rejection.

#### Scenario: Value returns to the caller executor

- **WHEN** a blocking callable returns a value
- **THEN** the awaiting Asio coroutine resumes exactly once on its associated executor with that value

#### Scenario: Void callable completes

- **WHEN** a blocking callable returns void
- **THEN** the awaiting caller resumes exactly once without a synthetic result value

#### Scenario: Move-only state crosses the boundary

- **WHEN** a move-only callable returns a move-only value
- **THEN** the value is transferred to the awaiting caller without requiring either object to be copied

#### Scenario: Callable throws

- **WHEN** a blocking callable throws an exception
- **THEN** the corresponding await expression rethrows that exception on the caller's execution domain

#### Scenario: Callable completes immediately

- **WHEN** the submitted callable returns before the initiating stack can otherwise continue
- **THEN** completion remains asynchronous and does not invoke the final caller continuation inline on that initiating stack

#### Scenario: Polling implementation is audited

- **WHEN** the blocking execution implementation and runtime traces are inspected
- **THEN** completion uses event publication and contains no `std::future` readiness loop, periodic completion timer, or fixed polling interval

### Requirement: Actor completion returns through ActorScheduler

`ActorContext::run_blocking()` SHALL store the blocking result or exception
before making the suspended actor continuation runnable through
`ActorScheduler`. A blocking worker or Asio callback MUST NOT directly resume
the actor coroutine. Immediate, successful, exceptional, cancellation, and
late-completion paths MUST publish at most one continuation.

#### Scenario: Successful actor completion becomes runnable

- **WHEN** an actor's blocking callable completes successfully
- **THEN** its continuation is enqueued exactly once and may be resumed by any eligible actor worker

#### Scenario: Actor observes a blocking exception

- **WHEN** an actor's blocking callable throws
- **THEN** an actor worker next resumes the task and the `run_blocking` await expression rethrows the stored exception

#### Scenario: Blocking worker never resumes actor inline

- **WHEN** a blocking callable reaches its terminal result
- **THEN** no actor statement after the await executes on the blocking worker or completion callback stack

### Requirement: Blocking suspension preserves mailbox exclusivity

An actor awaiting blocking work SHALL retain exclusive ownership of its
`actor_id + partition_key` mailbox. Later messages for the same mailbox MUST
remain FIFO queued until the active task completes or is cancelled. Runnable
mailboxes for other partitions MUST remain eligible for actor execution while
blocking work is pending.

#### Scenario: Same partition remains serial

- **WHEN** a first message awaits blocking work and a second message enters the same actor partition
- **THEN** the second handler does not start until the first handler completes or reaches terminal cancellation

#### Scenario: Independent partition progresses

- **WHEN** one actor partition is awaiting blocking work and another partition has runnable work
- **THEN** the other partition may execute without waiting for the first partition's callable

#### Scenario: Runtime does not infer partitions

- **WHEN** an actor is configured with one global partition and submits blocking work for unrelated conversations
- **THEN** the runtime preserves that single mailbox ordering instead of automatically deriving partitions from the callable or message

### Requirement: Cancellation and shutdown retain safe operation lifetime

Cancellation SHALL prevent an abandoned blocking operation from publishing an
actor continuation. Because an arbitrary synchronous callable is
non-preemptible, admitted operation state SHALL retain its callable captures,
actor object, and actor-library lifetime until a running callable and its
completion bridge retire. Runtime reload and process shutdown MUST NOT unload
actor code while an admitted callable can still execute it.

#### Scenario: Cancellation races with completion

- **WHEN** actor cancellation races with successful or exceptional blocking completion
- **THEN** at most one terminal actor transition wins and no abandoned actor coroutine is resumed

#### Scenario: Non-preemptible callable finishes late

- **WHEN** cancellation is requested after a synchronous callable has begun and the callable cannot be interrupted
- **THEN** the callable may finish while its captured resources and actor DSO remain valid, and its late result does not resume the abandoned actor

#### Scenario: Reload drains blocking actor work

- **WHEN** an old generation has an actor route awaiting blocking completion during reload
- **THEN** successful cutover waits for that route to retire, while a drain timeout keeps the old generation and process blocking executor active

#### Scenario: Process shuts down with admitted blocking work

- **WHEN** process shutdown begins while blocking callables are admitted
- **THEN** new blocking admission closes, admitted operations retire with runtime and actor lifetime intact, and the process-owned executor joins before those actor libraries are unloaded

### Requirement: Blocking service availability fails deterministically

Normal startup and reload candidates SHALL register the process
`BlockingExecutor` before accepting actor ingress. A manually constructed actor
context without the service and a stopped executor that rejects new admission
MUST report deterministic errors and MUST NOT allocate an implicit fallback
pool.

#### Scenario: Runtime service is missing

- **WHEN** an actor awaits `run_blocking` from a test or invalid context without a registered `BlockingExecutor`
- **THEN** the await fails with `BlockingExecutorUnavailable` and no callable is executed

#### Scenario: Executor admission is closed

- **WHEN** a caller submits after process blocking admission has closed
- **THEN** it receives one asynchronous stopped-executor error and no callable is executed
