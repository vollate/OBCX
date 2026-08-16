## Why

Blocking database, HTTP, and CPU-heavy work is currently submitted through
`IBot::run_heavy_task()` and an exposed per-bot `TaskScheduler`. That makes an
actor execution concern depend on whichever transport bot happened to deliver
the message, lets bot shutdown affect a process-shared worker pool, and forces
actors such as `chat_llm` to borrow a bot executor for unrelated timers and
coroutine orchestration. The current helper also polls a `std::future` with a
1 ms timer instead of publishing completion through an event-driven boundary.

The V2 runtime already budgets a distinct blocking-worker domain and already
has the safe `ActorTask` to Asio suspension boundary. It needs one
runtime-level blocking execution service and an actor-native API that use those
facilities without involving a bot instance.

## What Changes

- Add a process-owned, fixed-size `BlockingExecutor` created from the resolved
  runtime `blocking_workers` budget and registered in every active or candidate
  generation through `ActorServices`.
- Add `ActorContext::run_blocking(callable)` for actor code. It executes a
  synchronous callable on the blocking pool, suspends the current `ActorTask`
  without occupying an actor worker, and republishes completion through
  `ActorScheduler`.
- Provide an event-driven awaitable operation on `BlockingExecutor` for
  generation-tracked code that is already inside a nested Boost.Asio
  coroutine. It preserves value, void, move-only result, exception, and caller
  executor semantics without future polling or exposing the underlying thread
  pool.
- Preserve exclusive mailbox semantics while blocking work is in flight.
  Later messages for the same `actor_id + partition_key` remain queued; work
  for independent partitions may continue. The API does not infer or split
  partition keys.
- Make cancellation and shutdown safe for non-preemptible synchronous work:
  cancellation prevents an abandoned actor continuation from being published,
  while operation state and actor/library lifetime remain valid until a
  running callable retires.
- **BREAKING**: Remove `TaskScheduler`, `IBot::get_task_scheduler()`,
  `IBot::run_heavy_task()`, and task-scheduler constructor/state exposure from
  bot interfaces and implementations.
- Move bot event coroutine dispatch onto its non-blocking I/O execution path;
  event handlers that need blocking or CPU work must explicitly use the new
  runtime service.
- Migrate `chat_llm`, `obcx-actor-message-store`, `obcx-actor-bridge`, the
  actor template, registry validation, standalone fixtures, installed SDK
  headers, tests, and documentation to the bot-independent API.

## Capabilities

### New Capabilities

- `actor-blocking-execution`: Runtime blocking-service ownership, actor and
  nested-Asio awaitable APIs, scheduler return semantics, mailbox behavior,
  cancellation/lifetime safety, and removal of bot-coupled heavy-task access.

### Modified Capabilities

- `actor-runtime-operations`: Require the blocking worker allocation from the
  unified runtime thread budget to back one process-owned service shared by
  startup and reload generations, without creating worker threads during
  validation-only startup.

## Impact

- Public core and actor SDK headers, especially `ActorContext`, bot
  constructors/interfaces, installed header lists, and standalone actor
  fixtures.
- Main-process startup/shutdown ownership, runtime-generation service
  registration, reload candidate construction, and process-owned fingerprint
  behavior.
- `EventDispatcher`, QQ/Telegram bot wiring, `ComponentManager`, `chat_llm`,
  message-store persistence, and bridge repository/filesystem/media-conversion
  execution paths.
- Actor scheduler interoperability, shutdown/reload race coverage, sanitizer
  gates, and blocking-execution observability.
