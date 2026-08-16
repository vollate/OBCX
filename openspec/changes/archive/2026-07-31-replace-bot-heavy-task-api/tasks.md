## 1. Build The Event-Driven Blocking Executor

- [x] 1.1 Add focused tests for void, value, exception, move-only callable/result, immediate completion, caller-associated executor affinity, and stopped-executor rejection
- [x] 1.2 Implement the fixed-size process `BlockingExecutor` and its `async_run` initiating operation with exactly-once event publication and no future/timer polling
- [x] 1.3 Add the Boost.Asio `run` awaitable convenience, compile-time rejection for reference or awaitable results, and submitted/running/pending/completed/failed/rejected metrics
- [x] 1.4 Add deterministic tests proving an Asio I/O executor keeps processing unrelated handlers while a blocking callable is held on a blocking worker

## 2. Integrate Blocking Work With ActorTask

- [x] 2.1 Add actor tests for `ActorContext::run_blocking` value, void, exception, immediate completion, missing-service failure, and no inline resume on a blocking callback
- [x] 2.2 Extend scheduler-created `ActorContext` and the actor-to-Asio operation state with an actor/DSO lifetime lease that survives actor-frame abandonment until nested work retires
- [x] 2.3 Implement `ActorContext::run_blocking` by resolving the runtime service and returning completion through the existing ActorScheduler-safe Asio boundary
- [x] 2.4 Add one-actor-worker deterministic tests proving another partition progresses while the first awaits blocking work and the same partition remains FIFO exclusive
- [x] 2.5 Add forced cancellation-versus-completion and shutdown-versus-completion tests proving exactly-once terminal behavior and no late resume or captured-actor use-after-free

## 3. Wire Process And Reload Lifecycle

- [x] 3.1 Add startup, reload, and validation-only tests for blocking-service availability, exact service identity across generations, restart-required worker-count changes, and absence of validation worker threads
- [x] 3.2 Create the process-owned executor from the resolved `blocking_workers` budget and register it in the initial generation before actor ingress
- [x] 3.3 Propagate the active process executor into every reload candidate without constructing generation-local blocking threads
- [x] 3.4 Close blocking admission and order runtime drain, executor join, actor destruction, and DSO unload so admitted callables retain valid code and resources through process shutdown
- [x] 3.5 Add stable, payload-free lifecycle diagnostics and expose blocking submitted/running/pending/completed/failed/rejected counters through the runtime operations surface

## 4. Remove Bot-Coupled Scheduling

- [x] 4.1 Add EventDispatcher tests proving bot I/O continues while an event coroutine awaits actor ingress and no blocking pool is needed to initiate handlers
- [x] 4.2 Change EventDispatcher to use the bot's non-blocking I/O executor and document that synchronous handler work must cross the actor blocking boundary
- [x] 4.3 Remove scheduler constructor parameters, state, shutdown calls, getters, and heavy-task forwarding from `IBot`, QQ/TG bots, `ComponentManager`, and application bot creation
- [x] 4.4 Delete `TaskScheduler`, replace its installed SDK entry with `BlockingExecutor`, and update standalone core fixtures and CMake header audits
- [x] 4.5 Add production/source/installed-SDK negative audits for `TaskScheduler`, `get_task_scheduler`, `run_heavy_task`, compatibility forwarding, and the old 1 ms polling bridge

## 5. Migrate Chat LLM And Standalone Actors

- [x] 5.1 Refactor `chat_llm` initialization to resolve generation I/O and blocking services from `ActorContext`, with filesystem and repository setup executed off actor workers
- [x] 5.2 Move the outer message/command coroutine graph and runtime timers to the generation I/O executor without consulting a bot scheduler
- [x] 5.3 Migrate every database append/fetch/cleanup and synchronous LLM client call to `BlockingExecutor::run`, while leaving bot transport sends on their asynchronous I/O paths
- [x] 5.4 Update `chat_llm` fake bots, plugin fixtures, mock server tests, comments, and architecture/test documentation so no bot supplies or exposes a worker pool
- [x] 5.5 Run standalone `chat_llm` behavior tests for command, proactive, cleanup, error, reload, and shutdown flows against the installed bot-independent SDK

## 6. Document And Verify The Cutover

- [x] 6.1 Add actor-author and breaking-change documentation with `ActorContext::run_blocking` examples, nested-Asio guidance, partition/mailbox semantics, cancellation limits, and old-to-new API mapping
- [x] 6.2 Update runtime operations documentation for process ownership, unified thread budgeting, reload reuse, metrics, and shutdown ordering
- [x] 6.3 Run focused core and actor tests, the full CTest suite, installed-SDK smoke tests, and all affected local actor test suites
- [x] 6.4 Run blocking-operation, actor suspension, cancellation, reload, and shutdown suites under ThreadSanitizer and AddressSanitizer/UndefinedBehaviorSanitizer
- [x] 6.5 Record a repeatable pre/post completion-latency and throughput benchmark proving the replacement performs no timer polling and does not reduce independent-partition progress

## 7. Migrate The Remaining Standalone Actor Repositories

- [x] 7.1 Move `obcx-actor-message-store` schema initialization and message persistence behind `ActorContext::run_blocking`, and make its standalone fixture provide the process blocking and generation I/O services
- [x] 7.2 Make `obcx-actor-bridge` resolve the process `BlockingExecutor` independently of bots and route synchronous database, filesystem, and media-conversion work from its nested Asio graph through that executor
- [x] 7.3 Update `obcx-actor-template` to document and compile an actor-native `run_blocking` example while preserving synchronous handlers for non-blocking work
- [x] 7.4 Keep `obcx-actor-registry` metadata and generated index aligned with the migrated bridge and message-store packages, and validate that no retired bot-owned scheduling API is published
- [x] 7.5 Build and test every repository under `local_actor/` against a freshly installed SDK, then run a negative source audit for `TaskScheduler`, `get_task_scheduler`, and `run_heavy_task`
- [x] 7.6 Add a mock-bot-only bridge business simulation using the real runtime, actors, SQLite, repositories, and production-shaped partition/pipeline configuration; compare clean pre-cutover revisions without touching the production database
