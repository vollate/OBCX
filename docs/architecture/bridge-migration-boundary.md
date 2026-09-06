# Bridge Actor Boundary

Status: actor cutover complete in the checked-out bridge repository
(2026-07-13)

## Repository and entry point

The bridge lives at `local_actor/obcx-actor-bridge` and builds one dynamic
entry point: the ABI 2 `bridge` actor. Its package identity, artifact,
dependencies, compatibility range, and publication information are declared
in `actor.toml`.

`BridgeActor::handle` consumes
`obcx::message_store::events::MessageStored`, resolves the runtime
services it needs, and suspends its `ActorTask` through
`ActorContext::await_asio` while existing QQ/Telegram forwarding coroutines
run. The callback boundary republishes the actor continuation to the native
scheduler.

## Ownership boundary

Core owns:

- actor ABI, library loading, scheduling, cancellation, and pipeline dispatch;
- `DbManager`, process-owned bot installations/dispatcher, and installed SDK
  exports;
- installed SDK surface validation, generation-scoped configuration audit,
  and the clean cross-repository harness.

The bridge package owns:

- QQ-to-Telegram and Telegram-to-QQ forwarding behavior;
- group/topic mapping, reply/recall mapping, media conversion, and retry state;
- bridge-specific configuration and schema migrations;
- forwarding, mapping, media, retry, suspension, failure, and shutdown tests.

The bridge depends only on installed SDK headers. QQ and Telegram operations
are sent through `BotOperationGateway` with exact installation/surface DTOs.
Provider components, transports, and authenticated URLs remain process-owned.
Generic HTTP work uses the installed `HttpClient` API.

## Pipeline contract

The representative flow is:

```text
obcx::core::events::RawMessageEvent -> message_store ->
obcx::message_store::events::MessageStored -> bridge
```

The bridge emits `bridge::events::MessageForwarded` on success and
`bridge::events::MessageForwardFailed` with
a retryable failure on transient delivery errors. `source_platform` and
`conversation_id` form the recommended partition key so ordering is preserved
per conversation while unrelated conversations can run concurrently.

## Verification

```bash
ctest --test-dir build -R '^standalone_actor_v2_repositories$' \
  --output-on-failure
```

The harness installs a fresh SDK, configures the bridge against that prefix,
builds and installs its actor/metadata pair, and runs the repository test
suite. It also validates the bridge registry entry and artifact resolution.
