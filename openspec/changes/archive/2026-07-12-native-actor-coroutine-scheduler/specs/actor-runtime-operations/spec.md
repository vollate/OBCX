## ADDED Requirements

### Requirement: Runtime engine is selectable
Runtime configuration SHALL select `asio-v1` or `native-v2` without changing
actor pipeline definitions. During rollout, the process SHALL use only the
mailbox-state implementation belonging to the selected engine.

#### Scenario: Select native V2
- **WHEN** configuration selects `native-v2`
- **THEN** actor invocations use the native ActorTask scheduler and fixed V1 shard drainers do not execute them

#### Scenario: Roll back to V1
- **WHEN** configuration is changed from `native-v2` to `asio-v1` and the process restarts
- **THEN** compatible envelopes and ActorResult behavior continue through the V1 scheduler without depending on V2 in-memory mailbox state

### Requirement: Runtime owns one thread-budget policy
The runtime SHALL size actor workers, Asio I/O executors, and blocking/CPU
executors from one explicit thread-budget policy. An automatic value MUST avoid
independently allocating `hardware_concurrency()` threads to every pool.

#### Scenario: Automatic worker sizing
- **WHEN** worker counts are configured as automatic
- **THEN** startup resolves and logs a bounded allocation for actor, I/O, and blocking workers from one runtime budget

#### Scenario: Invalid explicit sizing
- **WHEN** configured worker counts are zero where zero is not automatic or exceed supported runtime limits
- **THEN** startup fails validation with an actionable configuration error

### Requirement: Scheduler state is observable
The V2 runtime SHALL expose counters or histograms for submissions,
backpressure, runnable/running/suspended mailboxes, queue depths, resume
duration, yields, steal attempts and successes, failures, cancellations, late
completions, and worker sleep/wake activity.

#### Scenario: Forced stealing updates metrics
- **WHEN** a stress test creates skew and an idle worker successfully steals a continuation
- **THEN** steal-attempt, steal-success, and relevant queue-depth metrics reflect the event

#### Scenario: Suspended mailbox is visible
- **WHEN** an actor is awaiting I/O
- **THEN** operational state reports it as suspended rather than runnable or actively consuming an actor worker

### Requirement: Diagnostics protect message contents
Scheduler diagnostics SHALL include task id, actor id, partition key, mailbox
generation, worker id, and elapsed timing where applicable. They MUST NOT log
message payloads by default.

#### Scenario: Slow actor warning
- **WHEN** an actor resume exceeds the configured warning threshold
- **THEN** a diagnostic identifies the execution context and duration without including payload or raw message fields

### Requirement: Concurrency verification is deterministic and stressable
The test suite SHALL include deterministic forced-placement tests for stealing
and forced interleavings for enqueue, completion, cancellation, and shutdown.
Timing alone MUST NOT be the only condition used to prove a steal occurred.

#### Scenario: Forced same-home-worker skew
- **WHEN** a test assigns multiple runnable mailboxes to one worker and holds another worker idle
- **THEN** the test deterministically observes a successful steal and validates exactly-once execution

#### Scenario: Repeated completion race
- **WHEN** seeded stress repeatedly races I/O completion, cancellation, and scheduler shutdown
- **THEN** no accepted invocation is lost, completed twice, resumed after destruction, or left hanging

### Requirement: Rollout is gated by recorded baselines
Before `native-v2` becomes the default, the project SHALL record V1 baselines
and SHALL pass agreed correctness, balanced-load, skewed-load, I/O-heavy,
completion-storm, shutdown, CPU, and memory acceptance thresholds. V1 rollback
MUST be exercised before default activation.

#### Scenario: V2 misses a rollout threshold
- **WHEN** a required correctness or performance threshold fails
- **THEN** `asio-v1` remains the default and V2 stays opt-in with the failing workload and metrics preserved

#### Scenario: V2 rollout succeeds
- **WHEN** all acceptance thresholds and standalone actor integration tests pass
- **THEN** `native-v2` may become the default while `asio-v1` remains selectable for the compatibility window
