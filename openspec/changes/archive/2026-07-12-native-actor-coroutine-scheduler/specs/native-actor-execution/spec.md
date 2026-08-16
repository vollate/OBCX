## ADDED Requirements

### Requirement: Scheduler-owned actor task lifecycle
The runtime SHALL provide a move-only standard C++20 `ActorTask<T>` whose first
resume, subsequent resumes, completion, cancellation, and destruction are
owned by ActorScheduler. An ActorTask MUST NOT begin executing when its return
object is created, and its final suspension MUST publish its outcome without
resuming arbitrary caller code inline.

#### Scenario: Newly created task remains suspended
- **WHEN** an actor handler returns a new ActorTask that has not been submitted to ActorScheduler
- **THEN** no actor handler statement executes until an actor worker claims and resumes the task

#### Scenario: Task publishes one outcome
- **WHEN** an ActorTask returns a value or exits with an exception
- **THEN** ActorScheduler publishes exactly one completion outcome and destroys the coroutine frame exactly once

### Requirement: Exclusive FIFO mailbox execution
The runtime SHALL maintain a FIFO mailbox for each `actor_id + partition_key`
and MUST allow at most one active ActorTask for that mailbox. A task suspended
for I/O SHALL retain mailbox ownership, and later messages MUST remain queued
until the active task completes or is cancelled.

#### Scenario: Same mailbox stays serial across suspension
- **WHEN** the first mailbox message suspends for I/O and a second message arrives for the same mailbox
- **THEN** the second actor handler does not begin until the first handler has resumed and completed or reached a terminal cancellation

#### Scenario: FIFO handoff after completion
- **WHEN** an active mailbox task completes while multiple messages are pending
- **THEN** the scheduler publishes the completed result before starting the oldest pending message

### Requirement: Runnable continuation work stealing
The native scheduler SHALL maintain worker-local runnable deques and SHALL
allow an idle actor worker to steal a runnable mailbox continuation from another
worker. The scheduler MUST NOT expose pending mailbox messages or I/O-suspended
continuations as stealable work.

#### Scenario: Idle worker steals forced skew
- **WHEN** one worker owns multiple runnable mailboxes and another worker has no local or injected work
- **THEN** the idle worker steals and executes at least one runnable mailbox without violating mailbox exclusivity

#### Scenario: Suspended task is not stolen
- **WHEN** an ActorTask is suspended awaiting I/O and has no runnable continuation
- **THEN** no worker deque contains that task until an exactly-once completion transition republishes it

### Requirement: Exactly-once runnable state transitions
Each mailbox generation SHALL publish at most one runnable continuation for a
given suspension point. A worker MUST successfully claim the runnable state
before resuming the task, and scheduler synchronization MUST establish a
happens-before relationship when a task migrates between workers.

#### Scenario: Concurrent completion and cancellation
- **WHEN** I/O completion and cancellation race for the same suspended mailbox generation
- **THEN** at most one transition publishes or terminates the continuation and no coroutine is resumed after destruction

#### Scenario: Worker migration observes actor state
- **WHEN** a continuation is queued by one worker and stolen by another
- **THEN** the stealing worker observes all actor-task state published before the queue operation

### Requirement: Cooperative execution and fairness
Actor execution SHALL be cooperative and SHALL run only until completion,
explicit yield, asynchronous suspension, or an observed cancellation point.
Completion of one message with additional mailbox work SHALL requeue the
mailbox instead of recursively running the next message inline. The runtime
MUST measure individual resume duration.

#### Scenario: Explicit yield republishes continuation
- **WHEN** actor code awaits `ActorContext::yield()`
- **THEN** the current resume returns to the worker loop and the task becomes runnable exactly once

#### Scenario: Slow resume is observable
- **WHEN** an actor resume exceeds the configured slow-resume threshold
- **THEN** the runtime records the actor, partition, worker, task identifier, and elapsed duration without logging the message payload

### Requirement: Backpressure accounts for all admitted work
Global, per-actor, and per-partition backpressure SHALL consistently count
queued, running, and I/O-suspended invocations until their terminal completion
or cancellation.

#### Scenario: Suspended invocation consumes capacity
- **WHEN** a mailbox invocation is suspended for I/O and its configured partition limit is reached
- **THEN** a new invocation for that partition is rejected with observable scheduler backpressure

#### Scenario: Completion releases capacity
- **WHEN** an invocation reaches terminal completion or cancellation
- **THEN** all applicable pending counters are decremented exactly once

### Requirement: Deterministic scheduler shutdown
The scheduler SHALL reject new submissions after shutdown begins and SHALL
support graceful-drain and cancelling shutdown modes. Worker threads MUST join
only after no runnable continuation can be republished, and every accepted
caller MUST receive a terminal result.

#### Scenario: Graceful shutdown drains runnable work
- **WHEN** graceful shutdown begins with accepted runnable invocations
- **THEN** those invocations complete while later submissions are rejected

#### Scenario: Cancelling shutdown includes suspended work
- **WHEN** cancelling shutdown begins with tasks suspended on asynchronous operations
- **THEN** cancellation is requested, late completions cannot resume abandoned tasks, and awaiting callers do not hang
