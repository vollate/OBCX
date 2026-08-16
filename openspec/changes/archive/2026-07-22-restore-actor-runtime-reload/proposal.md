## Why

The actor-only cutover correctly removed the legacy plugin reload path, but it
also left the operator command surface without any reload operation. As a
result, actor-owned configuration such as bridge group mappings cannot take
effect without restarting OBCX, even though the runtime already knows how to
validate actor contracts and pipelines before accepting ingress.

OBCX needs an actor-native reload lifecycle that preserves the actor-only
boundary, applies configuration predictably, and fails without taking the
working runtime offline.

## What Changes

- Add a `reload` operator command to both the TUI and `--no-tui` command
  interfaces. The command reloads the same root configuration path used at
  startup; it is not a plugin compatibility command.
- Treat reload as a runtime transaction: parse a candidate configuration,
  discover and validate candidate actor contracts, and run the same
  actor-aware configuration checks as startup before changing live state.
- Build a replacement actor-runtime generation from the candidate snapshot so
  newly constructed actors observe updated actor-owned sections, including
  bridge group mappings, actor settings, pipelines, and routing policy.
- Quiesce new actor ingress for the cutover, let already accepted work reach a
  defined terminal state within a bounded deadline, atomically publish the new
  runtime generation, and unload the retired generation only after no task or
  shared actor reference can still execute it.
- Stage actor libraries as generation-specific loads so a reload can adopt a
  rebuilt V2 actor binary without unloading code that is still referenced by
  the active generation. Actor-private shared dependencies receive
  content-versioned dynamic-link identities, not merely different staged
  paths, so old and candidate generations cannot bind to one another's private
  dependency images.
- Keep live bot connections and their `BotRegistry` process-owned. Startup and
  every candidate generation receive the same populated registry rather than
  constructing a generation-local empty replacement.
- Keep the current generation active when candidate parsing, contract
  validation, actor construction, activation, or cutover preparation fails.
  A failed reload reports a stable phase/reason diagnostic and does not leave a
  partially updated configuration or actor graph.
- Serialize concurrent reload requests and expose reload generation, phase,
  duration, drain outcome, and failure metrics without logging configuration
  secrets or message payloads.
- Define bot identity/transport changes, database-instance replacement, and
  scheduler thread-budget changes as restart-required in the initial reload
  contract. A candidate containing those changes is rejected explicitly rather
  than being applied partially.
- Preserve the actor-only cutover: do not restore `IPlugin`, `PluginManager`,
  plugin lifecycle callbacks, plugin manifests, or a plugin loader/reload
  branch.

## Capabilities

### New Capabilities

- `actor-runtime-reload`: Transactional operator-triggered configuration and
  V2 actor-generation reload, including validation, quiescence, atomic
  cutover, rollback, lifetime safety, restart-required boundaries, and
  operational diagnostics.

### Modified Capabilities

- `actor-runtime-operations`: Preserve the prohibition on plugin lifecycle and
  plugin reload entry points while permitting the new reload entry point owned
  exclusively by the V2 actor runtime.

## Impact

- CLI/TUI command handling and the application lifetime coordination in
  `src/app`, `src/common`, and `src/tui`.
- Configuration snapshot ownership and startup/reload validation paths in
  `ConfigLoader`.
- `ActorManager`, orchestrator ingress/routing generations, native scheduler
  drain/cancellation behavior, actor DSO lifetime management, and runtime
  metrics.
- Process-owned bot service assembly so every runtime generation shares the
  live, startup-populated `BotRegistry` without rebuilding bot connections.
- Actor packages whose constructors or handlers consume the root configuration,
  especially bridge mappings, plus integration, race, rollback, and shutdown
  tests across core, bridge, and message-store actors.
- Operator and breaking-change documentation must distinguish actor-native
  `reload` from the retired plugin reload command.
