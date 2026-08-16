## Why

OBCX still carries two obsolete extension paths—legacy plugins and V1/Asio
actors—beside the native V2 actor runtime, multiplying public APIs,
configuration formats, lifecycle rules, tests, and operational failure modes.
Now that the native coroutine scheduler and V2 ABI exist, OBCX should finish
the migration by making the actor model its only supported extension and
execution contract once the native runtime satisfies its recorded rollout
gates.

## What Changes

- Make `IActorV2`, `ActorTask`, actor pipelines, and the native work-stealing
  scheduler the only runtime extension and dispatch model.
- Gate the cutover on all native-v2 correctness, sanitizer, stress, shutdown,
  and performance thresholds, including the previously missed balanced-load
  threshold.
- **BREAKING**: Remove `IPlugin`, `PluginManager`, plugin lifecycle callbacks,
  plugin export symbols, plugin reload behavior, and the legacy plugin SDK and
  CMake helpers.
- **BREAKING**: Remove V1 `IActor`, the V1 factory ABI,
  `AsioActorV1Adapter`, `allow_v1_actors`, the `asio-v1` scheduler, and runtime
  engine selection.
- **BREAKING**: Remove plugin configuration and build metadata, including
  `[plugins]`, `plugins.toml`, `plugin.toml`, and plugin-specific config access.
  The actor-only runtime will not detect, translate, warn about, or otherwise
  handle these retired inputs.
- Standardize standalone components on actor metadata, actor SDK/CMake
  helpers, and V2 ABI discovery; rename active plugin-oriented directories,
  examples, documentation, and packaging surfaces to actor terminology.
- Migrate the bridge and message-store repositories to actor-only builds and
  replace the plugin registry with an actor registry whose schema and
  validation accept actor packages only.
- Add a coordinated roadmap and cross-repository release gates so removal in
  core cannot complete before every required actor repository builds, loads,
  and passes end-to-end smoke tests.

## Capabilities

### New Capabilities

- `actor-package-ecosystem`: Canonical actor package metadata, build helpers,
  registry schema, repository naming, and cross-repository conformance gates.

### Modified Capabilities

- `actor-abi-v2`: Make the V2 ABI exclusive by removing V1 coexistence,
  adaptation, factory discovery, and compatibility-window requirements.
- `actor-runtime-operations`: Replace selectable V1/V2 engines and post-cutover
  rollback with a gated transition to one mandatory native actor runtime, and
  remove the independent plugin lifecycle and retired-input handling.

## Impact

- Public headers and libraries under `include/interfaces`, `include/common`,
  `include/core`, `src/interfaces`, `src/common`, `src/core`, and `src/plugin`.
- Runtime construction, configuration loading, CLI reload behavior, installed
  SDK exports, CMake package files, vcpkg manifest generation, examples,
  packaging, tests, benchmarks, and generated/current documentation.
- Standalone `obcx-plugin-bridge`, `obcx-actor-message-store`, and
  `plugin-registry` repositories and the local checkout layout that currently
  hosts them under `local_plugin/`.
- The official plugin template is replaced by an actor-package template.
  Other plugin-only repositories currently referenced by local or remote
  `plugins.toml` entries are retired build inputs unless they independently
  migrate to the canonical V2 actor package contract; they are not release
  gates for this coordinated cutover.
- Existing plugin binaries, V1 actor binaries, plugin manifests, and
  `asio-v1` deployments will no longer load or run and receive no in-process
  compatibility or input-migration path.
