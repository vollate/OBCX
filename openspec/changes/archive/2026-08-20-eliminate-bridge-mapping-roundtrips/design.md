## Context

For a normal successful QQ-to-Telegram message, the current production path
is:

```text
QQHandler receives target id
  -> BridgeStateRepository::add_message_mapping()
  -> BridgeForwardingRuntime::get_target_message_id()
  -> BridgeForwardResult
  -> BridgeActor::add_message_mapping()
  -> MessageForwarded
```

The Telegram-to-QQ direct path has the same ownership split. The handler must
parse the bot response to learn the target id, but its public result is
`awaitable<void>`. `BridgeForwardingRuntime` therefore recovers the lost value
with a repository query. `IBridgeForwarder::forward_message()` already returns
`BridgeForwardResult`, and `BridgeActor` already treats that result as input to
its persistence and event-publication boundary, so the intermediate database
round trip is not required by the actor contract.

The pre-send mapping query is not redundant: it prevents a message already
known to be delivered from being sent again and its returned target id is
useful. Reply, recall, edit, retry, and media-group lookups likewise represent
business state rather than result reconstruction.

The business simulation currently reports that the new executor boundary adds
completion hops without increasing the capacity of the single SQLite writer.
Removing repository calls is therefore a prerequisite optimization before a
Redis buffer or weaker durability mode is evaluated.

## Goals / Non-Goals

**Goals:**

- Persist a newly delivered direct source-to-target mapping exactly once.
- Propagate the target id already present in the bot response without a
  post-send repository lookup.
- Make one layer responsible for the mapping durability guarantee behind
  `MessageForwarded`.
- Reuse the pre-send de-duplication result without another read or write.
- Preserve QQ-to-Telegram, Telegram-to-QQ, edit-resend, retry, reply, recall,
  and reload behavior.
- Prove repository-operation counts, not only final database row counts.

**Non-Goals:**

- Adding Redis, an in-memory write buffer, asynchronous acknowledgement, or a
  new database service API.
- Changing `message_store` raw-message or user persistence.
- Removing legitimate pre-send de-duplication, reply, recall, edit, cache, or
  retry queries.
- Changing SQLite durability, WAL, transaction, checkpoint, or filesystem
  settings.
- Redesigning the deferred Telegram media-group buffer or the retry worker's
  mapping-and-queue completion transaction.
- Claiming exactly-once delivery across the external bot send and local
  mapping commit.

## Decisions

### 1. Return a typed direct-forward outcome from platform handlers

The immediate QQ and Telegram forwarding handlers return an internal outcome
containing:

- source platform and source message id;
- target platform and target message id;
- whether the mapping is `new_delivery` or `already_persisted`.

An ordinary successful bot response produces `new_delivery`. A pre-send
de-duplication hit returns the mapping obtained by that existing query as
`already_persisted`. A skipped, buffered, failed, or retry-enqueued operation
uses an explicit non-forwarded status or failure instead of asking the
database afterwards whether forwarding might have happened.

Using a typed disposition was selected over an empty target id or a Boolean
because the actor must distinguish "persist this new mapping" from "this exact
mapping was already durable" without inferring state from another query.

### 2. Keep `BridgeForwardingRuntime` as a value-propagation boundary

`BridgeForwardingRuntime` attaches the resolved target bot identity and maps
the handler outcome directly into the public `BridgeForwardResult`. It does not
call `get_target_message_id()` after handler completion.

The public result carries enough disposition information for `BridgeActor` to
decide whether persistence is required. Test forwarders follow the same
contract, so a test cannot accidentally cause a second write merely because
the production handler already persisted one.

Returning a bare target id while retaining the post-send query as a fallback
was rejected because it would preserve the redundant path and make operation
counts data-dependent. Letting the runtime persist was rejected because the
actor is already the publication boundary and would either have to trust an
implicit side effect or retain its duplicate write.

### 3. Make `BridgeActor` the single direct-mapping commit owner

For `new_delivery`, `BridgeActor` validates the complete mapping, performs one
`add_message_mapping()` call through the existing blocking boundary, and emits
`MessageForwarded` only after the repository reports success. Platform
handlers and `BridgeForwardingRuntime` do not write that primary mapping.

For `already_persisted`, the actor validates the returned identity and emits
the existing completion behavior without another mapping write. The mapping
is known durable because it came from the handler's pre-send repository read.

If the single new-delivery write throws or reports failure, the actor emits no
`MessageForwarded` and reports a stable mapping-persistence failure. It does
not enqueue another bot send: the remote side effect may already have
succeeded, so blind resend could duplicate it.

### 4. Preserve special multi-record and retry ownership

The retry worker continues to own the atomic completion of a successful retry:
persisting its mapping and removing the retry row. Routing retry completion
back through `BridgeActor` would split that consistency boundary.

Telegram media-group flushing remains a deferred callback that may map several
Telegram source ids to one QQ target id and also update media-group state. It
continues to persist that set within its dedicated path. The immediate
`MessageStored` invocation must report a buffered/deferred disposition rather
than fabricate one direct `BridgeForwardResult` and trigger an actor write.

The QQ-to-Telegram media-group send is different: it is awaited inline and its
response already exposes the primary Telegram message id. That helper returns
the primary id to `QQHandler`, and the normal direct actor boundary persists it
once instead of writing it inside the formatter.

Reply, recall, edit preflight, user-cache, sticker-cache, and heartbeat
repository operations are not primary-result recovery calls and remain
unchanged. An immediate edit resend may return its replacement target mapping
to the actor for one upsert, but deferred media-group repair remains specialized.

### 5. Verify call counts and end-state semantics independently

Focused tests use an instrumented repository boundary or SQLite tracing to
count primary mapping operations. For each direct direction they prove:

```text
new delivery:       preflight read + one mapping write
already persisted:  preflight read + zero mapping writes
post-send path:      zero recovery reads
```

Tests also prove that the mapping is queryable before `MessageForwarded`, a
write failure never publishes success, and retry/media-group paths do not gain
an extra actor write.

The existing mock-bot business simulation remains the end-to-end performance
gate. It must validate operation counts and drain all work before reporting
throughput, so the result cannot be improved merely by deferring persistence.

## Risks / Trade-offs

- **[Bot send succeeds but the actor mapping write fails]** -> Report a
  mapping-persistence failure without automatic resend. This external-side-
  effect gap already exists and is not presented as exactly-once delivery.
- **[A handler returns an incorrect persistence disposition]** -> Use a closed
  enum, validate all mapping fields at the actor boundary, and cover production
  and test forwarders with contract tests.
- **[A duplicate message is emitted as forwarded again]** -> Preserve current
  completion behavior while reusing the already-durable mapping; do not issue
  another bot send or mapping write.
- **[Specialized paths are accidentally forced into the scalar result]** ->
  Keep retry completion and deferred Telegram media-group fan-out explicitly
  outside the direct mapping owner while returning the inline QQ media-group's
  primary id; test both ownership modes.
- **[Moving persistence outward increases the send-to-commit window]** -> Keep
  the handoff within the same awaited actor invocation and remove the recovery
  read and duplicate write, producing a shorter normal path overall.

## Migration Plan

1. Add operation-count and failure-injection tests around the current direct
   QQ and Telegram success paths.
2. Introduce the typed handler outcome and return target ids directly from bot
   responses and de-duplication hits.
3. Remove handler primary-mapping writes and the runtime recovery lookup, then
   enforce the one-write actor boundary.
4. Retain and verify retry, media-group, edit, reply, and recall behavior.
5. Re-run the isolated business benchmark and record both throughput and
   repository-operation counts.

Rollback restores the old propagation path. No schema or data migration is
needed because mapping keys and stored rows do not change.
