## 1. Establish operation-count coverage

- [x] 1.1 Add an instrumented bridge repository or SQLite trace fixture that distinguishes pre-send de-duplication reads, post-send recovery reads, and primary mapping writes
- [x] 1.2 Add QQ-to-Telegram and Telegram-to-QQ direct-success tests that expose the current write-read-write sequence before the refactor
- [x] 1.3 Add tests for an already-mapped source, incomplete forwarding result, and mapping-persistence failure without repeating the bot send
- [x] 1.4 Add regression coverage proving retry completion and deferred media-group flush do not receive an additional actor mapping write

## 2. Propagate forwarding outcomes directly

- [x] 2.1 Define a typed direct-forward disposition for new delivery, already-persisted delivery, and non-forwarded/deferred outcomes
- [x] 2.2 Change `QQHandler::forward_to_telegram` and its inline media-group helper to return the bot response target id or the existing de-duplication mapping and remove their primary mapping write
- [x] 2.3 Change immediate Telegram-to-QQ and edit-resend handling to return the corresponding target mapping while preserving deferred media-group behavior
- [x] 2.4 Change `BridgeForwardingRuntime::forward_message` to build `BridgeForwardResult` from the handler outcome and remove the post-handler `get_target_message_id` recovery query

## 3. Enforce one direct persistence owner

- [x] 3.1 Extend the `IBridgeForwarder` result contract so `BridgeActor` can distinguish a new mapping from one already proven durable
- [x] 3.2 Persist a new direct mapping exactly once in `BridgeActor` before publishing `MessageForwarded`
- [x] 3.3 Skip persistence for an already-mapped result while preserving the existing no-resend completion behavior
- [x] 3.4 Report missing mapping fields or repository failure without publishing success or blindly enqueuing another bot send
- [x] 3.5 Update bridge test doubles and documentation to reflect the explicit persistence disposition and single-owner boundary

## 4. Validate behavior and performance

- [x] 4.1 Run the bridge repository, handler, retry, media-group, and actor test suites against the freshly installed OBCX SDK
- [x] 4.2 Extend the isolated bridge business simulation to assert primary mapping operation counts and rerun the controlled tmpfs and production-filesystem comparisons without using the production database
- [x] 4.3 Record throughput, latency, final row counts, operation counts, and any remaining executor/database round trips in the benchmark evidence
- [x] 4.4 Run `openspec validate eliminate-bridge-mapping-roundtrips --strict --no-interactive`
