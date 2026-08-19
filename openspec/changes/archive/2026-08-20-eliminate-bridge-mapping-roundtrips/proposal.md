## Why

The direct bridge success path currently discovers the target message id in a
platform handler, persists that mapping in the handler, queries the same row
again in `BridgeForwardingRuntime` to reconstruct `BridgeForwardResult`, and
then persists the same mapping again in `BridgeActor`. Each repository call
crosses the blocking executor and the single SQLite writer, so one successful
delivery pays for one avoidable read and one avoidable write after the first
mapping write. The repeated `INSERT OR REPLACE` also obscures which layer owns
the durability boundary for `MessageForwarded`.

The bridge already has a result-bearing `IBridgeForwarder` boundary. Direct
forwarding should propagate the target id obtained from the bot response
through that boundary and commit the primary mapping exactly once before the
actor reports success.

## What Changes

- Introduce a typed direct-forward outcome that carries the complete
  source-to-target mapping and distinguishes a newly delivered message from a
  message proven by the pre-send de-duplication lookup to be already mapped.
- Make QQ-to-Telegram (including its immediately awaited media-group send),
  Telegram-to-QQ, and immediate edit-resend handlers return that outcome
  instead of persisting the primary mapping and returning `void`.
- Make `BridgeForwardingRuntime` build `BridgeForwardResult` directly from the
  handler outcome. It no longer queries the mapping repository after a
  successful send merely to recover the target id.
- Make `BridgeActor` the sole persistence owner for a newly delivered direct
  mapping. It performs one mapping write and emits `MessageForwarded` only
  after that write succeeds.
- Reuse the mapping returned by the existing pre-send de-duplication lookup.
  An already-mapped delivery performs no additional mapping read or write.
- Preserve the specialized persistence boundaries for retry completion and
  deferred Telegram media-group flushing; those paths can create multiple
  mappings or update retry state atomically and are not routed through the
  single-result direct-forward contract.
- Add focused operation-count, failure, and business-simulation coverage to
  prove the successful direct path performs one mapping write with no
  post-send recovery read or duplicate actor write.

## Capabilities

### New Capabilities

- `bridge-forwarding-mapping-persistence`: Explicit forwarding outcomes,
  single-owner persistence of direct source-to-target mappings, de-duplication
  reuse, and success/failure publication semantics.

### Modified Capabilities

None.

## Impact

- `local_actor/obcx-actor-bridge` handler result types, forwarding runtime,
  actor mapping persistence, and focused tests for both platform directions.
- Bridge test doubles implementing `IBridgeForwarder`, which must declare
  whether their returned mapping is new or already durable.
- The existing bridge business benchmark and evidence report, extended to
  assert mapping-operation counts as well as row counts and forwarding
  throughput.
- No database schema or stored-data migration is required.
- Redis buffering, asynchronous database acknowledgement, SQLite group
  commit, message-store message/user persistence, and blocking-executor API
  changes remain separate future work.
