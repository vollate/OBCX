# Native Actor Coroutine Runtime Decision

> Historical pre-cutover decision record. It documents the evaluation and
> compatibility phase, not current configuration or supported APIs. The
> current decision is [Actor Runtime Architecture
> Decision](actor-runtime-adr.md).

Status: superseded by the actor-only runtime decision on 2026-07-13

## Context

The V1 ActorScheduler hashes `actor_id + partition_key` mailboxes onto fixed
shard queues. One Boost.Asio coroutine drains each shard and awaits an actor
handler before advancing. This preserves mailbox FIFO order, but unrelated
mailboxes on the same shard block one another. Because `IActor` returns
`boost::asio::awaitable<ActorResult>`, continuation placement and resumption are
also owned by the enqueue caller's Asio executor rather than the actor runtime.

OBCX needs dynamic balancing of runnable actors without replacing Boost.Asio as
the networking and timer substrate. Existing standalone actors also require a
compatibility window.

## Decision

OBCX will add a V2 actor runtime based on a custom standard C++20 coroutine
return type named `ActorTask<T>`. The current message payload named `ActorTask`
will become `ActorInvocation`.

ActorScheduler will own a fixed worker pool. Runnable mailbox continuations
will live on worker-local double-ended queues, and idle workers may steal from
another worker. The stealable unit is one runnable mailbox continuation, never
an individual pending mailbox message.

Each `actor_id + partition_key` mailbox remains exclusive:

- at most one ActorTask is active for the mailbox;
- later messages remain FIFO queued;
- an ActorTask suspended for I/O retains mailbox ownership;
- I/O completion makes its continuation runnable again;
- a worker may resume that continuation only after claiming it.

Actor execution is cooperative. A worker runs actor code until completion,
explicit yield, cancellation observation, or asynchronous suspension. Blocking
and CPU-heavy work must use a dedicated middleware executor.

Boost.Asio remains responsible for ingress, networking, timers, and I/O. Two
explicit boundaries connect the runtimes:

1. Asio callers submit ActorInvocation through an Asio initiating operation and
   receive completion on their associated executor.
2. ActorTask suspends through an ActorContext Asio adapter. The I/O callback
   stores its outcome and enqueues the actor continuation; it never resumes
   actor code inline.

V2 introduces an explicit actor ABI generation and `IActorV2` returning
`ActorTask<ActorResult>`. V1 actors remain loadable through an adapter that runs
their Asio awaitable on an I/O executor and returns completion through the
native scheduler.

## Alternatives

### Per-mailbox Asio coroutines

Scheduling one Asio coroutine per mailbox would remove shard collisions with
less code. It was rejected as the V2 target because OBCX would still not own
continuation placement, cancellation, or work stealing.

### Adopt CAF

CAF provides a mature cooperative work-stealing actor scheduler. It was
rejected as a dependency because replacing OBCX actor identity, envelopes,
pipeline routing, and ABI would be a substantially larger migration.

### Steal individual messages

This was rejected because it permits concurrent processing within one mailbox
and violates partition FIFO ordering.

### Resume directly from I/O callbacks

This was rejected because actor state could execute on an I/O thread and race
mailbox cancellation or destruction.

## Consequences

- OBCX owns actor coroutine lifetime and can dynamically balance runnable work.
- Same-mailbox ordering remains strict even while awaiting I/O.
- The runtime gains a second coroutine domain and must test every boundary for
  exactly-once completion and late-callback safety.
- The first worker deque implementation uses locks and sleeping workers;
  lock-free queues are deferred until profiling justifies them.
- V1 remains the default and rollback engine until V2 passes sanitizer, stress,
  standalone actor, and recorded performance gates.

The detailed implementation and rollout gates live in
`docs/roadmaps/actor-runtime-v2-native-coroutine-roadmap.md` and OpenSpec change
`native-actor-coroutine-scheduler`.
