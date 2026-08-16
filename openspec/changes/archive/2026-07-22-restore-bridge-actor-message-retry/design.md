## Context

The bridge handlers already contain the enqueue decision and the
`RetryQueueManager` already persists retry rows, restores them, applies
exponential backoff, writes a message mapping after a successful resend, and
removes completed rows. The missing actor-mode integration is the owner around
that machinery.

`BridgeForwardingRuntime` currently passes `nullptr` to both handlers. The old
plugin implementation instead created a managed Asio worker, restored the
database queue, registered a Telegram callback for QQ-origin messages and a QQ
callback for Telegram-origin messages, and stopped the worker before tearing
down its executor. The actor-only implementation must reproduce those delivery
semantics through `BotRegistry` and generation ownership rather than reviving
`IPlugin` or its global bot lookup.

Reload adds a lifecycle constraint that the plugin implementation did not
have: two actor generations can coexist during candidate preparation and
cutover. A retry worker cannot be allowed to run in both generations against
the same persistent queue.

## Goals / Non-Goals

**Goals:**

- Make `enable_retry_queue = true` operational for actor-mode QQ-to-Telegram
  and Telegram-to-QQ sends.
- Preserve retry rows and successful message mappings across process restart
  and actor-runtime reload.
- Resolve live bots only through the process-owned `BotRegistry` already
  shared with every runtime generation.
- Give the retry worker one explicit owner and a bounded, race-free shutdown
  sequence.
- Make tests deterministic without waiting for production-scale backoff
  intervals.

**Non-Goals:**

- Adding automatic retries to the general actor orchestrator or every
  retryable `ActorFailure`.
- Providing exactly-once delivery across an upstream API response loss; the
  bridge remains an at-least-once sender with idempotent local queue identity.
- Expanding the currently unused media-download retry path.
- Restoring plugins, plugin callbacks, plugin manifests, or plugin reload.
- Supporting multiple live QQ or Telegram accounts in one bridge generation;
  the existing single-bot-per-platform lookup contract remains unchanged.

## Decisions

### 1. Let `BridgeForwardingRuntime` own one managed retry worker

When the actor first resolves its real forwarding runtime, the runtime creates
one worker only if `enable_retry_queue` is true. The worker owns its
`io_context`, work guard, thread, and `RetryQueueManager`. It registers all
callbacks before starting queue processing, restores persistent rows before
the first timer iteration, and passes the same manager to both handlers.

A dedicated worker executor is retained because retry timers and callbacks
outlive the actor invocation that detected the send failure. It also gives the
bridge a synchronous stop-and-join boundary independent of the generation's
actor I/O pool. Creating one worker per handler was rejected because both
workers would restore and race on the same persistent queue.

When retry is disabled, no retry executor or thread is created and the handlers
receive `nullptr`. This keeps the existing explicit opt-out behavior.

### 2. Register platform callbacks through `BotRegistry`

The worker captures shared ownership of the process-owned `BotRegistry`, not
raw generation-local bot pointers. For each retry attempt it resolves the
target platform and sends:

- Telegram group messages with `send_group_message`, or topic messages with
  `send_topic_message` when `target_topic_id > 0`; and
- QQ group messages with `send_group_message`.

The Telegram callback accepts only a response containing
`result.message_id`. The QQ callback accepts only a response containing
`data.message_id`. A missing bot, exception, empty response, or invalid
response shape returns an unsuccessful attempt and leaves retry accounting to
the manager. Callback diagnostics identify the target platform and attempt but
do not include message content or credentials.

Capturing bots found during initialization was rejected because bot lookup is
process-owned and callbacks may execute much later. Reusing the retired plugin
global bot list was rejected because it violates the actor-only boundary.

### 3. Make the configured message policy authoritative

The worker receives an immutable policy derived from the bridge generation's
configuration:

- `message_retry_max_attempts`;
- `message_retry_base_interval_sec`;
- `retry_queue_check_interval_sec`; and
- `max_retry_interval_sec`.

Attempt and interval values must be positive, and the base/check intervals
must not exceed the maximum interval. Invalid values fail actor-aware
configuration validation instead of silently falling back to compiled
constants. Existing rows retain their stored attempt count and maximum; the
active generation policy controls scheduling of subsequent attempts.

Queue identity remains unique by source platform, source message id, and
target platform. Adding the same failed send updates that durable entry rather
than creating parallel retries.

### 4. Preserve queue and mapping state transactionally from the worker's view

On an initial send failure, the handler enqueues the complete outgoing message
and target metadata before returning failure to the actor pipeline. The actor
may still emit a retryable `MessageForwardFailed`; queued delivery is
background work and does not pretend the initial attempt succeeded.

On a successful retry, the manager writes the source-to-target message mapping
and removes the retry row. These operations use the bridge repository and are
covered by failure-injection tests so a database failure cannot be reported as
a successful cleanup. On a failed retry, the manager increments the attempt,
records the failure reason and next time, and leaves the row durable. Exhausted
rows are removed according to the existing finite-attempt policy and produce a
terminal diagnostic.

This proposal does not claim upstream exactly-once behavior. If the upstream
accepted a message but its response was lost, a retry can duplicate it; local
mapping and queue updates remain consistent with the response the bridge
observed.

### 5. Give generation retirement an explicit retry-worker stop boundary

`BridgeForwardingRuntime::shutdown()` is idempotent and non-throwing. It first
prevents new callback work, then asks the manager to stop, cancels its timer on
the worker executor, waits for an in-flight resend callback to finish or
cancel, releases the work guard, stops the `io_context`, joins the thread, and
only then releases handlers, repositories, and callback captures. Its
destructor invokes this path defensively.

After the old generation's routed work drains during reload, core releases the
retired scheduler and actor-manager references so the bridge actor and
forwarding runtime shut down while their DSO and services are still valid.
This retirement happens before gated post-cutover ingress is released. The
candidate bridge worker remains lazy, so it cannot begin restoring rows during
candidate preparation. The result is a single retry owner across the cutover.

Process shutdown uses the same ordering. Direct timer cancellation from an
unrelated thread and detached fire-and-forget teardown were rejected because
they can race suspended Asio operations and actor DSO unloading.

### 6. Report disabled, unavailable, queued, and exhausted states distinctly

The existing "消息发送失败且未启用重试" diagnostic is used only when the
generation explicitly disables retry. If retry is enabled but worker creation,
callback registration, bot resolution, or persistence setup fails, actor
forwarding reports a stable retry-unavailable failure and does not describe the
feature as disabled. Successful enqueue logs the queue identity and next
attempt metadata without logging the outgoing message.

## Risks / Trade-offs

- **[A resend callback hangs during retirement]** -> Apply the bot action
  timeout, stop admission first, and bound worker shutdown; test cancellation
  and no late callback after DSO retirement.
- **[Old and new generations both restore rows]** -> Keep candidate startup
  lazy and stop/release old actor instances before reopening cutover ingress.
- **[The worker thread adds per-generation resources]** -> Create exactly one
  only when retry is enabled and synchronously join it on every teardown path.
- **[Database mapping succeeds but queue deletion fails]** -> Preserve the
  unique mapping key, surface the cleanup failure, and make a repeated cleanup
  safe; do not claim upstream exactly-once delivery.
- **[Invalid retry settings cause surprising timing]** -> Validate the policy
  with the same immutable actor configuration snapshot used by the generation.

## Migration Plan

1. Add failing actor-mode tests proving enabled retry currently receives a null
   manager in both forwarding directions.
2. Introduce the managed worker, callback wiring, and immutable retry policy
   while keeping disabled behavior unchanged.
3. Add explicit bridge shutdown and generation retirement ordering, followed
   by reload/shutdown race tests.
4. Run bridge unit/integration tests, root actor-runtime tests, and sanitizer
   coverage for the worker lifecycle.

Rollback removes the managed worker integration and retirement-order changes;
the durable retry table is schema-compatible and requires no data migration.
