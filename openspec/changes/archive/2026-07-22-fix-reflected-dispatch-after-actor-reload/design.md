## Context

The installed actor reload path stages a new copy of each actor DSO for every
runtime generation. The logged failure occurred after repeated successful
reloads: the new message-store actor advertised and received the canonical
`obcx::core::events::RawMessageEvent` type, but its generated dispatcher
returned `unsupported_message_type` and no new bridge mapping was persisted.

`ReflectedActor` built a function-local static vector containing
`std::string_view` canonical names and actor-specific dispatch function
pointers. On ELF/GCC, the vector's guard and storage were emitted with
`STB_GNU_UNIQUE` binding. Loading another staged copy of the same actor DSO
could therefore reuse the first copy's table even after runtime generation
publication and retirement, crossing the DSO lifetime boundary that staging is
intended to establish.

Two adjacent issues amplified the incident. The reload gate woke waiters using
`steady_timer::cancel()`, which has no lasting effect if the posted cancellation
runs just before `async_wait()` is initiated. The application event callback
also discarded non-throwing route failures, leaving only indirect symptoms in
the actor log and database.

## Goals / Non-Goals

**Goals:**

- Ensure reflected handler selection always uses metadata and code from the
  actor instance's current loaded generation.
- Preserve exactly-once admission for bot messages racing with reload gate
  closure and reopening.
- Surface structured ingress failures in production logs.
- Reproduce the original multi-generation failure through real bot event
  dispatch, bridge forwarding, message-store routing, retry, and persistence.

**Non-Goals:**

- Changing canonical message names, actor contracts, or the actor ABI.
- Replacing generation-specific DSO staging or changing linker isolation for
  actor-private dependencies.
- Adding a new retry policy or changing the bridge retry semantics.
- Logging message payloads or every failure in a multi-failure route.

## Decisions

### 1. Select reflected handlers directly in the current DSO

Generate a linear selection block with the existing C++26 expansion statement.
For the matching canonical message name, construct the exact handler's
`ActorTask<ActorResult>` immediately and return it to the coroutine that drives
the task. Unsupported types still return the existing stable failure.

This removes the actor-specific function-local static vector, including its
dispatch function pointers and `string_view` elements. Compile-time validation,
canonical-name storage, JSON decoding, sync/async normalization, task runtime
attachment, and input lifetime behavior remain unchanged.

A per-DSO static table with different visibility was rejected because its
correctness would continue to depend on compiler and linker symbol binding.
The generated direct selection is explicit about the required lifetime and
keeps the small handler-set lookup on the existing linear path.

### 2. Expire reload waiters instead of only cancelling them

When the gate state changes, post `expires_at(steady_clock::time_point::min())`
to each published waiter. An already registered wait completes with timer
cancellation, while a wait initiated after the posted handler observes an
already expired deadline and completes immediately. The gate state remains the
authority checked by the waiter loop.

This closes the publish-before-wait race without blocking an executor thread or
adding a second synchronization primitive.

### 3. Log structured non-throwing ingress failures

The bot event callback checks the returned `OrchestratorResult`. When it is not
successful, it logs the platform, bot identity, failure count, first failing
pipeline/stage/actor, stable failure code, retryability, and safe failure
message. Payloads and credentials are not logged.

Only the first failure is expanded to keep one root message from producing an
unbounded log line while preserving the total failure count.

### 4. Exercise the exact production topology in the regression

The installed actor smoke test uses a shared `TaskScheduler`, real
`EventDispatcher`, process-owned bot registry, reload controller, staged bridge
and message-store actors, and the configured database. It performs repeated
cold reloads before the first message, exercises a failed send and retry, then
performs more reloads and dispatches messages during candidate preparation,
while the ingress gate is closed, and after cutover.

The test requires successful ingress results and persisted message mappings for
all three boundary positions. This topology reproduced the original
`unsupported_message_type` before the dispatch change and passes with the
current-generation selection.

## Risks / Trade-offs

- **[Handler selection executes comparisons for each message]** -> Reflected
  actors normally have a small handler set, the prior table was also searched
  linearly, and direct selection avoids allocation plus unsafe cached dispatch
  pointers.
- **[Expiring a waiter reports operation cancellation]** -> The existing gate
  loop already treats the timer as a wakeup mechanism and rechecks authoritative
  gate/shutdown state.
- **[Ingress diagnostics expose application data]** -> Log only routing
  metadata and the existing safe failure message; never log envelope payloads
  or configuration values.
- **[The integration regression becomes more involved]** -> Keep the sequence
  deterministic with explicit generation, gate, result, retry, and database
  assertions rather than timing-only success.

## Migration Plan

No operator or actor migration is required. Deploy core and actor packages
built against the same existing SDK contract, then run the repeated installed
actor reload smoke test before release. Rollback is the previous core build;
configuration and persisted bridge data remain compatible.

## Validation Evidence

- `reflected_actor_test`: 8/8 passed.
- `runtime_reload_controller_test`: 7/7 passed.
- Installed actor reload smoke completed with
  `reload=ok old_routes=1 new_routes=3 bot_reconnects=0`.
- ThreadSanitizer `runtime_reload_controller_test`: 7/7 passed with no report.
- `nix fmt -- --fail-on-change` reported no changed files.
- `git diff --check` passed.
