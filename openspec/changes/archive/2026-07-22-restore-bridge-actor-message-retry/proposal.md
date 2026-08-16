## Why

`tests/bridge_actor.toml` already sets `enable_retry_queue = true`, but the
actor-mode `BridgeForwardingRuntime` constructs both forwarding handlers with a
null retry manager. A failed QQ-to-Telegram or Telegram-to-QQ send therefore
logs `消息发送失败且未启用重试`, does not enqueue the message, and leaves the
configured persistent retry path unused.

The actor-only bridge needs to own and operate the retry worker that the legacy
plugin path previously initialized, without restoring any plugin lifecycle or
creating duplicate retry owners across actor-runtime reload generations.

## What Changes

- Create one bridge-generation-owned message retry worker when
  `actors.bridge.config.enable_retry_queue` is true, and pass its manager to
  both forwarding handlers instead of `nullptr`.
- Register QQ and Telegram resend callbacks against the process-owned
  `BotRegistry`, including Telegram topic sends and platform-specific response
  message-id extraction.
- Restore persisted retry rows, honor the configured attempt and backoff
  limits, and keep successful retry mapping writes and queue cleanup atomic
  from the worker's point of view.
- Distinguish an explicitly disabled queue from retry-worker initialization or
  callback failures; enabled retry must not fall through to the misleading
  "未启用重试" diagnostic.
- Tie worker startup, cancellation, in-flight callback drain, and executor
  teardown to the bridge actor generation. During reload, stop the retired
  generation's worker before releasing post-cutover ingress so only one
  generation can process persisted retries.
- Add focused bridge actor, retry-manager, restart/reload, and shutdown tests
  for both forwarding directions.

## Capabilities

### New Capabilities

- `bridge-actor-message-retry`: Actor-native, persistent retry of failed bridge
  message sends, including configuration, bot callback wiring, mapping
  persistence, generation ownership, and clean shutdown.

### Modified Capabilities

None.

## Impact

- `local_actor/obcx-actor-bridge` forwarding runtime, retry manager,
  configuration validation, and tests.
- Actor-generation retirement ordering in core where needed to release actor
  instances and their background workers before old generation resources are
  retired or post-cutover ingress is resumed.
- `tests/bridge_actor.toml` remains the runtime example with
  `enable_retry_queue = true`; no plugin compatibility path is reintroduced.
