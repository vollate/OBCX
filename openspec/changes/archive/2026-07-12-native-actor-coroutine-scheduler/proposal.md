## Why

The current actor scheduler binds mailboxes to fixed shard drainers and exposes
`boost::asio::awaitable` in the actor ABI, so unrelated mailbox collisions can
block one another and OBCX cannot independently place or steal suspended actor
continuations. The actor ecosystem is still small, making this the lowest-risk
time to establish a scheduler-owned coroutine boundary while retaining a V1
compatibility path.

## What Changes

- Add a move-only standard C++20 `ActorTask<T>` coroutine whose lifecycle,
  suspension, resumption, cancellation, and completion are owned by the actor
  runtime.
- Add scheduler-owned actor workers with local runnable deques, external work
  injection, idle-worker stealing, cooperative yielding, and deterministic
  shutdown.
- Replace fixed-shard execution in the V2 engine with an exclusive mailbox
  state machine that preserves FIFO order while allowing runnable
  continuations to migrate between workers.
- Add explicit Asio-to-actor and actor-to-Asio adapters. Asio remains the
  networking, timer, and I/O substrate, but its callbacks may only make actor
  continuations runnable and may not resume actor code inline.
- Add a versioned `IActorV2` ABI returning `ActorTask<ActorResult>`, explicit ABI
  generation discovery, V2 export helpers, and a V1 Asio actor adapter for a
  compatibility window.
- Add runtime selection, worker configuration, scheduler metrics, stress tests,
  benchmarks, and a tested `asio-v1` rollback path.
- **BREAKING**: Rename the public scheduling payload `ActorTask` to
  `ActorInvocation` so `ActorTask<T>` can name the V2 coroutine type. Existing
  V1 actor factory symbols and `IActor` remain supported during migration.

## Capabilities

### New Capabilities

- `native-actor-execution`: Scheduler-owned `ActorTask` lifecycle, cooperative
  execution, mailbox exclusivity, work stealing, backpressure, and shutdown.
- `actor-asio-interoperability`: Bidirectional asynchronous boundaries that
  preserve executor affinity, values, exceptions, cancellation, and actor
  scheduler ownership.
- `actor-abi-v2`: Versioned V2 actor discovery and loading plus safe coexistence
  and adaptation of V1 actors.
- `actor-runtime-operations`: Runtime configuration, observability, stress
  verification, performance gates, rollout, and rollback behavior.

### Modified Capabilities

None. The repository has no existing OpenSpec capability specifications; V1
behavior is retained as a compatibility engine while the new capabilities are
introduced.

## Impact

- Core actor API, scheduler, orchestrator integration, actor manager, runtime
  construction, configuration loader, task scheduler boundaries, and installed
  SDK/CMake helpers.
- Standalone `message_store` and bridge actors, fixture actors, cross-repository
  smoke tests, and actor author migration documentation.
- New actor worker threads and synchronization primitives alongside existing
  Asio I/O and blocking/CPU executors, requiring a unified thread-budget policy.
- No replacement of Boost.Asio networking or bridge QQ/Telegram forwarding
  internals; those remain Asio coroutines behind the interoperability layer.
