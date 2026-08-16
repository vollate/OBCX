## Context

OBCX V1 represents a submitted message with `ActorTask` and exposes
`boost::asio::awaitable<ActorResult>` from `IActor::handle_message`. The actor
scheduler hashes `actor_id + partition_key` onto one of a fixed number of shard
queues. A single Asio coroutine drains each active shard and awaits the complete
actor handler before selecting another mailbox. This preserves FIFO order for a
mailbox but creates head-of-line blocking when unrelated mailboxes share a
shard. It also makes actor execution inherit the enqueue caller's Asio executor
and leaves the actor runtime without a resumable unit it can place or steal.

The runtime already has separate concerns that must remain compatible:

- Boost.Asio owns bot ingress, sockets, HTTP/WebSocket operations, and timers.
- ActorScheduler owns partition ordering and backpressure.
- The orchestrator is currently an Asio coroutine and awaits scheduler results.
- Standalone actors are loaded through a shared-library ABI.
- Bridge forwarding contains a deep tree of existing Asio coroutines that must
  not be rewritten as part of this change.
- V1 actors and the legacy plugin path require a migration window.

The target model follows cooperative actor scheduling: actor code runs until it
completes, yields, or suspends on an asynchronous boundary. An actor worker
never waits for I/O. I/O completion publishes a runnable actor continuation
back to the native scheduler.

## Goals / Non-Goals

**Goals:**

- Give OBCX ownership of actor coroutine creation, resumption, cancellation,
  completion, and destruction.
- Preserve one-at-a-time FIFO processing for each actor partition across
  worker migration and I/O suspension.
- Dynamically balance runnable mailboxes with scheduler-owned work stealing.
- Keep Boost.Asio as the I/O implementation while making it an explicit runtime
  boundary rather than the actor coroutine ABI.
- Support V1 and V2 actors in the same runtime during migration.
- Provide deterministic shutdown, cancellation, diagnostics, stress coverage,
  benchmarks, and a configuration rollback path.

**Non-Goals:**

- Preempt arbitrary C++ actor code.
- Run two messages from the same mailbox concurrently.
- Replace Boost.Asio networking, timers, or bridge forwarding internals.
- Introduce distributed work stealing or durable mailboxes.
- Adopt CAF as a runtime dependency.
- Implement a lock-free deque before a mutex-based scheduler is correct and
  measured.
- Remove the V1 actor ABI in the same change that introduces V2.

## Decisions

### 1. Use a native standard C++20 ActorTask type

Add a move-only `ActorTask<T>` with a custom `promise_type`. It initially
suspends so only ActorScheduler can begin execution. Its promise stores the
result or exception, scheduler attachment, mailbox generation, cancellation
state, and completion target. Its final suspension reports completion to the
scheduler and never resumes a caller inline.

Rename the current scheduling payload `ActorTask` to `ActorInvocation`. The
payload remains plain message data, while `ActorTask<T>` unambiguously denotes
an executable coroutine.

Alternative considered: continue using `boost::asio::awaitable` and schedule
one coroutine per mailbox. This would remove fixed-shard blocking with less
code, but would leave continuation placement, lifecycle, and cancellation under
the Asio executor and would not provide native work stealing.

Alternative considered: adopt CAF. CAF provides the desired scheduling model,
but adopting its actor runtime would duplicate OBCX actor concepts and require a
larger ABI and messaging migration than implementing the limited native task
contract OBCX needs.

### 2. Schedule mailbox continuations, not messages

A mailbox contains FIFO pending invocations and at most one active ActorTask.
Its active task moves through `runnable`, `running`, and `suspended` states. A
runnable queue contains an `ActorContinuation` that identifies the mailbox and
coroutine frame; it never contains a second continuation for the same mailbox
generation.

When a task completes, its result is published before the scheduler either
creates the next task from the mailbox head or transitions the mailbox to idle.
While a task is suspended for I/O, later messages remain queued and cannot
start. This preserves the V1 serialization contract and actor-state safety.

Alternative considered: steal individual messages. This could allow more
parallelism but violates FIFO and permits concurrent access to one partition's
actor state.

### 3. Use worker-local deques with correctness-first synchronization

ActorScheduler owns a configurable set of actor worker threads. Each worker has
a mutex-protected double-ended queue. The owner pushes and pops from its local
end; an idle worker steals from the opposite end of a randomly selected victim.
External submissions use a global injector or low-contention round-robin
injection. I/O resumptions prefer the task's previous worker for locality but
remain stealable.

Workers sleep using a runnable-work counter plus a condition variable or
counting semaphore. Publication and wake-up use one defined protocol so no
worker remains asleep while runnable work exists. A lock-free deque and NUMA
victim selection are deferred optimizations with independent benchmarks.

Alternative considered: a single global ready queue. It is simpler and remains
an available policy for testing or low-load systems, but the default V2 policy
uses local queues to reduce central contention and retain locality.

### 4. Keep execution cooperative

Calling `resume()` runs an ActorTask until completion, explicit
`ActorContext::yield()`, an OBCX asynchronous await, an Asio bridge await, or a
cancellation point. The scheduler cannot preempt a coroutine between those
points. Resume duration is measured and a configurable threshold emits a
warning. CPU-heavy and blocking work must use dedicated middleware executors.

One actor message is the initial fairness quantum. Completion of a message with
more mailbox work republishes the mailbox rather than recursively running the
next handler inline.

### 5. Bridge the two coroutine domains in both directions

The Asio-to-actor boundary is an Asio initiating operation:

```cpp
template <typename CompletionToken>
auto async_enqueue(ActorInvocation invocation, CompletionToken &&token);
```

The operation captures the completion handler and associated executor. Actor
completion posts the handler to that executor, allowing the existing
orchestrator to continue using `use_awaitable` without running actor code as an
Asio coroutine.

The actor-to-Asio migration boundary is
`ActorContext::await_asio(executor, factory)`. It suspends the ActorTask,
`co_spawn`s the existing Asio awaitable, stores its value or exception, and
publishes the ActorContinuation when the Asio completion fires. The callback
never invokes `coroutine_handle::resume()` itself. A generation and atomic
completion state make completion exactly once and reject late callbacks after
cancellation.

After the migration adapter is stable, add an OBCX Asio completion token using
`async_initiate` so actor code can await compatible Asio operations without an
extra nested Asio coroutine frame.

Alternative considered: resume the actor directly from the I/O callback. This
reduces one queue hop but bypasses mailbox state, permits execution on an I/O
thread, and makes exactly-once cancellation unsafe.

### 6. Introduce an explicit V2 ABI and retain V1 through an adapter

Add `IActorV2::handle_message` returning `ActorTask<ActorResult>`, an explicit
numeric ABI-generation symbol, versioned factory symbols, and
`OBCX_ACTOR_EXPORT_V2`. Actor semantic version remains separate from ABI
generation. ActorManager discovers the ABI before casting a factory result and
rejects unsupported generations at load time.

V1 actors continue to load through existing symbols. `AsioActorV1Adapter`
implements the V2 execution boundary by starting the V1 Asio awaitable on an
I/O executor and publishing completion back to ActorScheduler. V1 and V2 actors
may therefore share one scheduler without treating V1 coroutine frames as
native tasks.

Alternative considered: replace `IActor` in place. This is rejected because a
shared-library ABI mismatch could load successfully and fail during dispatch.

### 7. Make cancellation and runtime ownership explicit

Scheduler shutdown first closes admission. Runnable tasks either drain or are
cancelled according to shutdown mode. Suspended operations request underlying
Asio cancellation when supported, but their shared completion state remains
alive until the operation acknowledges completion. A late callback may release
state but cannot republish an abandoned mailbox generation.

No actor task is detached without runtime-owned tracking. Worker threads join
only after the scheduler proves no runnable continuation can be published.
Awaiting callers receive a defined cancellation result rather than polling or
hanging.

### 8. Roll out as a selectable engine with a unified thread budget

Add `actor_runtime.scheduler.engine = "asio-v1" | "native-v2"` and a stealing
policy configuration. V1 remains the initial default. Actor workers, Asio I/O
threads, and blocking/CPU workers are selected from one runtime thread budget
to avoid multiplying `hardware_concurrency()` across pools.

V2 exposes runnable/running/suspended mailboxes, queue depths, resume duration,
steal attempts and successes, cancellations, late completions, and worker
sleep/wake activity. Payload contents are excluded from default logs.

## Risks / Trade-offs

- **Coroutine frame use-after-free** -> Keep operation state runtime-owned, use
  mailbox generations and exactly-once transitions, and gate rollout on ASan,
  UBSan, and cancellation stress tests.
- **Lost or duplicate resume** -> Centralize state transitions and require one
  atomic publication path for yields, I/O completions, and cancellation.
- **Lost worker wakeup** -> Couple a global runnable count with the sleep/wake
  primitive and test forced interleavings repeatedly under TSan.
- **Two coroutine domains increase complexity** -> Restrict crossings to two
  named adapters and trace every boundary with task, mailbox, and operation ids.
- **Cooperative actor monopolizes a worker** -> Measure resume duration, warn on
  overruns, document yield points, and offload blocking/CPU work.
- **Actor object is shared across partitions** -> Preserve the documented V1
  thread-safety requirement and test mailbox exclusivity separately from actor
  instance concurrency.
- **V2 is slower at light load** -> Benchmark against V1, allow a global-sharing
  policy, sleep idle workers, and retain configuration rollback.
- **Thread oversubscription** -> Centralize actor, I/O, and blocking pool sizing.
- **Shared-library incompatibility** -> Discover ABI generation before factory
  use, retain V1 symbols, and maintain cross-repository build tests.
- **Nested Asio bridge adds allocation and frame overhead** -> Use it for safe
  migration, then benchmark a direct completion-token adapter.

## Migration Plan

1. Record V1 correctness and performance baselines, including forced shard
   collisions and I/O-completion storms.
2. Rename the scheduling payload and add ActorTask plus a deterministic test
   scheduler without changing production dispatch.
3. Add the native worker pool and mailbox state machine behind `native-v2`.
4. Add both Asio boundaries, cancellation, shutdown, and operational metrics.
5. Add ABI-generation discovery, IActorV2, export helpers, and the V1 adapter.
6. Run the orchestrator test suite against both selectable engines.
7. Migrate message_store, then migrate only the outer bridge actor boundary;
   retain its internal Asio forwarding graph.
8. Run sanitizer, forced-race, million-transition, and workload benchmarks.
9. Make V2 the default only after recorded correctness and performance gates
   pass. Retain `asio-v1` for one compatibility window.
10. Remove V1 only in a later change after external actor repositories and the
    rollback exercise pass.

Rollback requires only selecting `asio-v1`. V1 and V2 keep compatible message
envelopes and ActorResult behavior, and their internal mailbox state is not
shared, so a process restart can safely return to V1.

## Open Questions

- What measured Phase 0 thresholds will gate balanced throughput, skewed queue
  delay, tail latency, CPU usage, and memory overhead?
- Should the initial wake-up primitive use `std::condition_variable` for broad
  platform support or `std::counting_semaphore` where supported?
- How many releases constitute the V1 compatibility window?
- Should a global work-sharing policy ship with V2 or remain test-only until a
  low-power deployment requests it?
- Which direct Asio operations should first support the OBCX completion token
  after the generic `await_asio` migration adapter is proven?
