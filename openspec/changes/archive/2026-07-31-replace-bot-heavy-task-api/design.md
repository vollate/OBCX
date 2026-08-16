## Context

The current execution path mixes three different concerns:

```text
bot event
  -> EventDispatcher on TaskScheduler
  -> actor-runtime ingress
  -> ActorScheduler / ActorTask
  -> bot.get_task_scheduler() for nested Asio orchestration
  -> bot.run_heavy_task() for database, HTTP, or CPU work
```

`TaskScheduler` is constructed as one process-shared object in `main`, but it is
typed and exposed as bot state. QQ and Telegram bot shutdown can stop it, and
actors must first resolve a bot merely to reach the pool. Its heavy-task bridge
posts a callable, stores the result in `std::promise`, and polls the matching
`std::future` with a 1 ms `steady_timer`.

V2 already separates actor workers from an Asio I/O pool and reserves
`blocking_workers` in the unified process thread budget. `ActorContext` already
supports `await_asio`, whose operation state stores completion and republishes
the actor continuation through `ActorScheduler` rather than resuming an actor
on an I/O callback thread. The missing piece is a concrete blocking service and
a narrow API built on that boundary.

The following constraints shape this design:

- an actor mailbox is exclusive per `actor_id + partition_key`, including while
  its active task is suspended;
- runtime reload builds a candidate generation while the active generation and
  process-owned resources remain live;
- actor code can reside in reloadable DSOs, so a late blocking completion must
  not outlive the actor object or its library;
- synchronous blocking functions cannot be safely preempted by framework
  cancellation;
- validation-only startup must not create worker threads or external activity;
- in-tree `chat_llm` helpers already form a nested Boost.Asio coroutine graph,
  while the outer reflected actor boundary returns `ActorTask`.

## Goals / Non-Goals

**Goals:**

- Make blocking execution a runtime capability that is independent of bot
  identity, transport, and lifecycle.
- Provide `ActorContext::run_blocking()` as the normal actor-author API.
- Support existing generation-tracked nested Asio coroutines without forcing a
  wholesale rewrite to another coroutine type.
- Execute blocking callables only on the resolved blocking-worker domain and
  return results without polling.
- Preserve values, void completion, move-only callables/results, exceptions,
  caller executor affinity, and exactly-once actor continuation publication.
- Keep actor objects, coroutine state, and actor DSO code alive until admitted
  blocking work has retired.
- Remove the old `TaskScheduler` and bot-facing heavy-task surface completely,
  then migrate all in-tree callers and installed SDK fixtures.

**Non-Goals:**

- Automatically choosing or splitting actor partition keys.
- Allowing a second message in the same mailbox to run while the first message
  awaits blocking work.
- Running asynchronous socket/timer operations in the blocking pool; those
  remain on the Asio I/O domain.
- Preempting arbitrary synchronous functions, injecting a thread cancellation,
  or inventing a generic timeout for work that does not cooperate.
- Supporting callables that return references or another awaitable.
- Adding an independent `hardware_concurrency()` pool per bot, actor, or
  generation.
- Adding a new blocking-queue configuration surface in this change. Existing
  actor admission/backpressure bounds the normal actor-originated workload;
  queue limits can be proposed separately if measurements require them.
- Keeping a deprecated `IBot::run_heavy_task()` forwarding shim.

## Decisions

### 1. Own one `BlockingExecutor` at the process-runtime boundary

Startup creates one `std::shared_ptr<BlockingExecutor>` after configuration and
thread-budget validation and before ingress begins. Its fixed worker count is
the resolved `RuntimeThreadBudget::blocking_workers`. The application, not any
bot or actor generation, owns shutdown.

The initial generation receives the service through `ActorServices`. Reload
candidate requests inherit the exact same service from the active generation
before candidate publication. `blocking_workers` remains part of the
process-owned fingerprint, so changing it during reload still returns
`reload_restart_required`. Validation-only startup validates the resolved
budget but does not construct the executor.

The service does not expose its underlying `boost::asio::thread_pool` as a
general coroutine executor. Callers can submit only through its blocking
operation API. This prevents actors from moving arbitrary asynchronous
orchestration or long-lived timers onto the blocking domain.

Creating one executor per generation was rejected because candidate
preparation would temporarily duplicate blocking threads and a thread-budget
change could leak across the existing restart-required boundary. Keeping one
executor per bot was rejected because it recreates the ownership problem and
multiplies thread counts with bot count.

### 2. Use an event-driven Asio initiating operation

`BlockingExecutor` exposes one primitive and one awaitable convenience:

```cpp
template <BlockingCallable Func, typename CompletionToken>
auto async_run(Func&& function, CompletionToken&& token);

template <BlockingCallable Func>
auto run(Func&& function)
    -> boost::asio::awaitable<std::invoke_result_t<Func&>,
                              boost::asio::any_io_executor>;
```

`async_run` is implemented with `boost::asio::async_initiate`. It captures the
decayed callable and completion handler in shared operation state, obtains the
handler's associated executor and work guard, and posts exactly one execution
closure to the blocking pool. The closure stores a value or exception and
posts exactly one completion to the associated executor. Submission rejection
and an immediately returning callable still complete asynchronously.

`run` is a convenience over `async_run(..., use_awaitable)` for code already in
a Boost.Asio coroutine. It is not tied to a bot and does not choose an actor
continuation path itself.

The operation supports void and value results and move-only state. A concept or
static assertion rejects reference results and awaitable-returning callables:
the callable represents synchronous work whose complete dynamic extent belongs
on a blocking worker.

The current promise/future plus timer polling was rejected because it adds
timer wakeups and up to 1 ms of avoidable completion latency. Switching the
entire caller coroutine to the blocking executor was rejected because code
after the blocking call could then run on the wrong execution domain.

### 3. Make `ActorContext::run_blocking()` the actor-facing API

Actor code uses:

```cpp
auto records = co_await context.run_blocking(
    [repository, query] { return repository->fetch_context(query); });
```

`ActorContext::run_blocking()` resolves `BlockingExecutor` from actor-local or
generation services and adapts `BlockingExecutor::run()` through the existing
safe actor-to-Asio boundary. A missing service produces a deterministic
`BlockingExecutorUnavailable` failure rather than a null dereference or an
implicit fallback pool.

While the operation is pending:

1. `ActorTask` reports `AwaitingIo`;
2. the actor worker returns to the scheduler;
3. the synchronous callable runs on a blocking worker;
4. completion is stored outside the actor coroutine;
5. the mailbox continuation becomes runnable through `ActorScheduler`;
6. an actor worker resumes the await expression with its value or exception.

The blocking worker never calls `resume()` on the actor coroutine. Completion
may be claimed by any eligible actor worker, exactly like other external
continuations.

A free function taking a bot or a raw thread-pool executor was rejected because
it would preserve transport coupling or let callers bypass scheduler return
semantics. Making `BlockingExecutor` a global singleton was rejected because it
would hide test/runtime ownership and complicate validation-only startup.

### 4. Preserve mailbox ownership and require explicit partition design

Suspending on `run_blocking()` releases an actor worker but retains the active
mailbox. Messages with the same `actor_id + partition_key` remain FIFO queued
until the active invocation completes or is cancelled. Messages for different
partitions remain independently schedulable and can use other actor or blocking
workers.

Therefore a global or overly broad partition still serializes unrelated
conversations even after blocking work is offloaded. Actor configuration should
split independent state domains first, for example by
`source_platform:conversation_id`, and then use `run_blocking()` inside each
partition. The runtime does not infer a safe partition from callable captures
or database keys.

Releasing the mailbox during a blocking await was rejected because another
message could mutate the same actor/conversation state before the first
invocation resumes, violating the V2 exclusive FIFO contract.

### 5. Retain actor and DSO lifetime across late completion

The scheduler supplies `ActorContext` with an actor-lifetime lease derived from
the active invocation's shared actor wrapper. Actor-to-Asio operation state
retains this lease until its nested coroutine and completion callback retire.
This covers both direct `context.run_blocking()` and a generation-tracked outer
`context.await_asio()` whose nested coroutine uses `BlockingExecutor::run()`.

Cancellation marks the actor operation abandoned and prevents late completion
from publishing a continuation. It may signal Asio cancellation, but a running
synchronous callable is allowed to finish. Its callable, captured resources,
actor object, and DSO lease remain valid until then.

Normal reload does not cancel the process executor. The old generation's route
drain includes the actor invocation waiting for blocking completion; successful
cutover therefore cannot unload its actor while the callable is active. A drain
timeout retains the old generation as it does today.

Process shutdown closes ingress and blocking admission first, keeps runtime
and actor code alive while already-admitted blocking calls retire, then joins
the blocking executor before unloading the remaining actor generation. Domain
operations such as HTTP calls must retain their own bounded timeouts because
the framework cannot safely terminate an arbitrary C++ function.

Destroying an abandoned actor frame immediately without a lifetime lease was
rejected because in-tree callables capture `this`, repositories, and code from
the actor DSO. Stopping the shared pool from an individual bot was rejected
because other bots and active generations can still have admitted work.

### 6. Remove scheduling state from bots

`IBot`, `QQBot`, and `TGBot` no longer accept, store, return, or stop a
`TaskScheduler`. `ComponentManager::create_bot()` no longer receives one.
`TaskScheduler` and its installed SDK header are removed.

`EventDispatcher` stores an `any_io_executor` for the bot's existing
`io_context` and uses it only to initiate non-blocking event coroutines. Those
coroutines normally submit actor ingress and suspend, allowing the same
`io_context` to continue network work. Any handler that performs synchronous
blocking or CPU work must route it through the actor runtime and
`run_blocking()`.

Continuing to pass `BlockingExecutor` into bot constructors solely for event
dispatch was rejected because it would leave pool lifetime entangled with bot
lifetime and would spend blocking capacity on coroutine orchestration.
Creating a new event thread pool was rejected because it would bypass the
unified process thread budget.

### 7. Migrate `chat_llm` onto generation services

`chat_llm` resolves both of its non-bot execution dependencies from
`ActorContext`:

- the generation's `boost::asio::any_io_executor` for its nested coroutine
  graph and cleanup timers;
- the process `BlockingExecutor` for synchronous database, filesystem, cleanup,
  and LLM-client calls.

The actor's outer handlers continue using `context.await_asio()` to host the
existing nested Boost.Asio graph. Helpers inside that tracked graph call
`co_await blocking_executor->run(...)`. Direct `ActorTask` code and future
actors use `context.run_blocking()` instead.

Initialization becomes suspension-safe: configuration parsing may remain on
the actor worker, while prompt-file reads and repository initialization run on
the blocking executor. TTL timer callbacks initiate cleanup through the
blocking API instead of running SQLite work on the actor I/O executor. Bot send
operations remain ordinary asynchronous transport calls.

This migration removes all uses of `get_task_scheduler()` and
`run_heavy_task()` from `chat_llm`, including tests and comments. Rewriting the
entire nested coroutine graph from Boost.Asio awaitables to `ActorTask` in this
change was rejected because it is unrelated to the ownership defect and would
greatly expand the behavioral migration.

### 8. Apply the same execution-domain boundary to every standalone actor

The cutover is incomplete if only `chat_llm` compiles against the new SDK.
The remaining repositories under `local_actor/` have no textual dependency on
`run_heavy_task`, but some still execute synchronous work on actor or
generation-I/O threads:

- `obcx-actor-message-store` performs SQLite migration and persistence from a
  synchronous reflected handler;
- `obcx-actor-bridge` performs synchronous repository, filesystem, and media
  conversion work inside its nested Asio forwarding graph;
- `obcx-actor-template` is the canonical authoring surface and therefore must
  show the new actor-owned boundary without teaching that all ordinary work
  belongs on the blocking pool;
- `obcx-actor-registry` must continue publishing metadata generated from the
  migrated actor packages and must not expose a compatibility dependency on
  the retired bot-owned scheduler.

`message-store` converts its reflected handler to
`ActorTask<ActorResult>` and uses `ActorContext::run_blocking()` for the
complete schema-initialization/write transaction. `bridge` resolves the same
process `BlockingExecutor` from `ActorContext` and passes it into the
generation-tracked forwarding runtime; nested Asio helpers use
`BlockingExecutor::run()` only around synchronous database, filesystem, and
conversion calls. Bot sends, timers, and socket operations remain on their
asynchronous executors.

Standalone tests must provide both `BlockingExecutor` and
`boost::asio::any_io_executor`, matching the production service contract.
Every actor repository is built against a freshly installed SDK so an in-tree
header cannot accidentally hide an incomplete migration.

### 9. Verify execution domains and regressions explicitly

Focused tests use latches/barriers rather than sleeps to prove:

- a callable runs on a blocking worker and not on the actor worker;
- with one actor worker, a blocked invocation does not prevent a different
  partition from completing;
- the same partition remains exclusive while suspended;
- value, void, move-only result, exception, immediate completion, and stopped
  executor paths complete exactly once;
- nested Asio completion resumes on the caller-associated executor;
- cancellation and shutdown never resume an abandoned actor or destroy its
  actor/DSO lifetime early;
- startup and reload generations observe the same process executor;
- validation-only startup creates no blocking threads;
- installed headers and production sources contain no old bot scheduling API
  or 1 ms future-polling bridge.

ThreadSanitizer covers operation-state publication and executor shutdown races.
AddressSanitizer/UndefinedBehaviorSanitizer cover abandoned frames, captured
actor resources, and reload/unload lifetime. A focused benchmark compares
completion latency and throughput against a recorded pre-change
`TaskScheduler` baseline and confirms that the new path performs no timer
polling.

An additional bridge business simulation loads the real message-store and
bridge actors through `RuntimeGenerationBuilder`, retains the real SQLite,
repository, mapping, formatting, partition, and pipeline paths, and mocks only
external bot calls. It derives a credential-free temporary configuration from
the production bridge shape and forces both database configuration fields into
a unique sandbox. This benchmark reports bridge throughput separately from
co-tenant actor responsiveness: releasing an actor worker while waiting for the
single SQLite writer does not by itself increase that writer's capacity.

## Risks / Trade-offs

- **[A synchronous callable never returns]** -> Require domain-level timeouts
  and cooperative cancellation where available; document that process shutdown
  cannot safely preempt arbitrary C++ code.
- **[A broad partition still causes head-of-line blocking]** -> Document that
  suspension retains mailbox ownership and add a one-worker/two-partition
  conformance test that makes the configuration responsibility visible.
- **[Blocking-pool saturation increases latency]** -> Keep the pool fixed to the
  unified budget, expose submitted/running/pending/completed/failed/rejected
  counters, and rely on existing actor admission limits for normal actor work.
- **[Bot I/O is blocked by a legacy synchronous event handler]** -> Audit and
  migrate every in-tree event handler, document the non-blocking handler
  contract, and test that bot I/O progresses while actor ingress is suspended.
- **[Cancellation unloads actor code too early]** -> Retain a scheduler-supplied
  actor/DSO lifetime lease in every actor-to-Asio operation until its final
  callback retires, then stress cancellation and reload under sanitizers.
- **[Nested Asio code bypasses actor tracking]** -> Permit
  `BlockingExecutor::run()` only inside a runtime-tracked coroutine boundary;
  do not add detached actor work, raw pool access, or a global singleton.
- **[Breaking SDK removal affects external actors]** -> Ship a concise migration
  note with before/after examples and make installed-SDK compilation fail
  clearly at the removed bot methods rather than retaining ambiguous behavior.

## Migration Plan

1. Add `BlockingExecutor`, its event-driven initiating operation, and focused
   value/exception/executor-affinity tests without wiring production callers.
2. Add actor-lifetime leases and `ActorContext::run_blocking()` with mailbox,
   cancellation, and sanitizer tests.
3. Create the process-owned executor from the resolved thread budget, register
   it in startup and reload generations, and enforce shutdown ordering and
   validation-only behavior.
4. Move `EventDispatcher` to the bot I/O executor and remove `TaskScheduler`
   from bot interfaces, implementations, `ComponentManager`, and `main`.
5. Migrate `chat_llm` initialization, timers, database work, and LLM calls to
   generation I/O and blocking services; update its standalone fixtures and
   documentation.
6. Migrate the remaining standalone actor repositories by execution domain and
   validate their registry metadata against the installed SDK.
7. Update installed SDK manifests and migration guides, run strict source
   audits, focused benchmarks, full tests, TSan, and ASan/UBSan.

Before release, rollback is a normal revert of this change. After release the
old bot API is not retained in-process; an external actor must migrate to
`ActorContext::run_blocking()` or the tracked nested-Asio service API before
building against the new SDK.

## Open Questions

None. The service ownership, API boundary, mailbox behavior, and direct removal
of the bot-coupled API are selected explicitly.
