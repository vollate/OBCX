## 1. Baseline And Runtime Decision

- [x] 1.1 Add an ADR that records the native ActorTask, mailbox suspension, work-stealing, and explicit Asio-boundary decisions
- [x] 1.2 Add a V1 scheduler regression test that forces two unrelated mailboxes onto the same shard and records the current head-of-line behavior
- [x] 1.3 Add repeatable V1 benchmark workloads for balanced mailboxes, skewed mailboxes, I/O suspension, completion storms, and shutdown under load
- [x] 1.4 Record V1 throughput, queue-delay, tail-latency, CPU, and memory baselines and define V2 rollout thresholds
- [x] 1.5 Add validated configuration scaffolding for `asio-v1` and opt-in `native-v2` without changing the production default

## 2. Native ActorTask Primitive

- [x] 2.1 Rename the scheduling payload `ActorTask` to `ActorInvocation` across public headers, core sources, tests, SDK examples, and migration notes
- [x] 2.2 Implement move-only `ActorTask<void>` with initial suspension, explicit ownership, final suspension, and safe frame destruction
- [x] 2.3 Generalize ActorTask to value results with `ActorTask<T>` result storage and exception propagation
- [x] 2.4 Add scheduler, mailbox generation, task identifier, cancellation state, and completion-target attachment to the ActorTask promise
- [x] 2.5 Ensure final suspension publishes one outcome without resuming caller code inline and add value, void, exception, and destruction tests
- [x] 2.6 Add a deterministic single-thread test scheduler that explicitly resumes ActorTask continuations
- [x] 2.7 Implement `ActorContext::yield()` and cancellation observation with deterministic yield and cancellation tests
- [x] 2.8 Run ActorTask lifecycle tests under AddressSanitizer and UndefinedBehaviorSanitizer and fix all reported ownership defects

## 3. Native Work-Stealing Executor

- [x] 3.1 Implement ActorScheduler-owned worker startup, worker identifiers, and joined shutdown lifecycle
- [x] 3.2 Implement a mutex-protected double-ended runnable queue for each actor worker
- [x] 3.3 Implement external continuation injection and preferred-worker local requeue
- [x] 3.4 Implement randomized victim selection and opposite-end stealing of runnable continuations
- [x] 3.5 Implement the global runnable counter and worker sleep/wake protocol without polling or lost wakeups
- [x] 3.6 Define and test queue synchronization that publishes actor state before a continuation can migrate to another worker
- [x] 3.7 Implement graceful-drain and cancelling worker-pool shutdown modes
- [x] 3.8 Add deterministic forced-skew tests proving successful stealing and exactly-once synthetic continuation execution
- [x] 3.9 Run repeated worker startup, stealing, sleep/wake, and shutdown stress under ThreadSanitizer

## 4. V2 Mailbox Scheduling

- [x] 4.1 Implement mailbox generations and explicit idle, runnable, running, suspended, and stopping states
- [x] 4.2 Enforce one active ActorTask and FIFO pending ActorInvocation order for each actor and partition mailbox
- [x] 4.3 Publish each completed ActorResult before requeueing the mailbox for its next pending invocation
- [x] 4.4 Keep I/O-suspended tasks exclusively attached to their mailbox while later messages remain pending
- [x] 4.5 Port global, per-actor, and per-partition backpressure so queued, running, and suspended work is counted exactly once
- [x] 4.6 Implement exactly-once transitions for yield, completion, cancellation, and late-callback races using mailbox generation checks
- [x] 4.7 Add the V2 scheduler path without fixed shard assignment while leaving the V1 shard implementation intact
- [x] 4.8 Add mailbox tests for FIFO order, suspension ownership, cross-worker migration, hash-collision independence, and counter release
- [x] 4.9 Add seeded stress tests for enqueue versus completion, cancellation versus completion, and shutdown versus requeue

## 5. Boost.Asio Interoperability

- [x] 5.1 Implement `ActorScheduler::async_enqueue` as an Asio initiating operation with completion-token support
- [x] 5.2 Deliver async-enqueue completion through the handler's associated executor with value, failure, and cancellation tests
- [x] 5.3 Replace V2 promise/future timer polling with event-driven runtime completion state
- [x] 5.4 Implement `ActorContext::await_asio` for void and value-returning Asio awaitables on an explicitly selected executor
- [x] 5.5 Store Asio values and exceptions before publishing the actor continuation back to ActorScheduler
- [x] 5.6 Add immediate and delayed completion tests proving that an Asio callback never resumes actor code inline
- [x] 5.7 Propagate supported Asio cancellation and retain safe operation state for non-cancellable or late-completing operations
- [x] 5.8 Add forced cancellation-versus-success tests proving exactly one result and no post-destruction resume
- [x] 5.9 Prototype an OBCX Asio completion token with `async_initiate` and verify behavioral parity with the generic await-asio adapter

## 6. Actor ABI V2 And V1 Compatibility

- [x] 6.1 Define an explicit numeric actor ABI-generation contract and versioned V2 factory symbol names
- [x] 6.2 Add `IActorV2` returning `ActorTask<ActorResult>` and add `OBCX_ACTOR_EXPORT_V2`
- [x] 6.3 Extend ActorManager to discover ABI generation before casting factory results and reject unsupported generations at load time
- [x] 6.4 Implement `AsioActorV1Adapter` that starts V1 awaitables on an I/O executor and returns completion through the native scheduler
- [x] 6.5 Add V1, V2, mixed-version, missing-symbol, and unsupported-generation fixture actor tests
- [x] 6.6 Update installed SDK headers and actor CMake helpers so a standalone V2 actor builds, loads, and handles a message
- [x] 6.7 Add configuration validation for enabling or rejecting V1 actors during the compatibility window
- [x] 6.8 Document the ActorInvocation source rename, IActorV2 migration, compiler compatibility, and V1 compatibility policy

## 7. Orchestrator And Runtime Integration

- [x] 7.1 Parse native scheduler engine, policy, worker count, slow-resume threshold, and V1 compatibility configuration
- [x] 7.2 Implement one runtime thread-budget resolver for actor, Asio I/O, and blocking or CPU worker pools
- [x] 7.3 Construct and own the selected actor scheduler engine in the runtime bundle
- [x] 7.4 Route awaited orchestrator stages through the common async-enqueue facade for both V1 and V2 engines
- [x] 7.5 Replace detached terminal-stage scheduling with runtime-owned task tracking that obeys shutdown
- [x] 7.6 Preserve emitted-message recursion, pipeline dependency order, backpressure failures, and ActorFailed routing on V2
- [x] 7.7 Parameterize actor scheduler and orchestrator behavior tests to run against both engines
- [x] 7.8 Add runtime destruction tests proving no actor continuation, terminal task, timer poll, or completion callback is left hanging

## 8. Standalone Actor Migration

- [x] 8.1 Convert message_store's actor interface and export to IActorV2 without changing repository behavior
- [x] 8.2 Update message_store standalone build and smoke tests for native ActorTask dispatch
- [x] 8.3 Convert the outer bridge actor interface and export to IActorV2 while retaining its QQ and Telegram Asio coroutine graph
- [x] 8.4 Adapt bridge forwarding through `ActorContext::await_asio` and remove direct dependency on the actor coroutine's Asio executor
- [x] 8.5 Add bridge tests proving I/O suspension releases the actor worker, retains mailbox ownership, and returns mapping results or failures
- [x] 8.6 Add mixed V1 and V2 pipeline integration tests with equivalent envelopes, ordering, and failure semantics
- [x] 8.7 Verify standalone message_store and bridge repositories build against the installed V2 SDK in the cross-repository harness

## 9. Operational Visibility

- [x] 9.1 Add counters for accepted and rejected submissions, runnable, running, suspended, completed, failed, and cancelled invocations
- [x] 9.2 Add per-worker queue-depth, injector-depth, steal-attempt, steal-success, worker-sleep, and worker-wake metrics
- [x] 9.3 Add histograms for mailbox queue delay, actor resume duration, and end-to-end invocation latency
- [x] 9.4 Add slow-resume diagnostics containing task, actor, partition, mailbox generation, worker, and elapsed time without message payloads
- [x] 9.5 Add tests that force stealing, suspension, cancellation, and slow resumes and assert the corresponding metrics and privacy behavior

## 10. Verification, Rollout, And Rollback

- [x] 10.1 Run core, fixture actor, message_store, and bridge tests under AddressSanitizer and UndefinedBehaviorSanitizer
- [x] 10.2 Run native scheduler and interoperability race suites under ThreadSanitizer with deterministic seeds
- [x] 10.3 Complete at least one million synthetic enqueue, resume, suspend, completion, cancellation, and shutdown transitions without loss, duplication, hang, or post-destruction resume
- [x] 10.4 Benchmark V2 against the recorded balanced, skewed, I/O-heavy, completion-storm, shutdown, CPU, and memory baselines
- [x] 10.5 Keep `asio-v1` as the default and preserve failing seeds and metrics if any required correctness or performance threshold misses
- [x] 10.6 Exercise a process-restart rollback from `native-v2` to `asio-v1` with compatible envelopes and results
- [x] 10.7 Enforce making `native-v2` the default only after all gates pass; activation was correctly withheld because the balanced-load gate failed
- [x] 10.8 Retain selectable `asio-v1` and V1 actor loading for the agreed compatibility window and track their eventual removal as a separate change
- [x] 10.9 Update runtime architecture, actor author, configuration, operations, and rollback documentation with the verified V2 behavior
