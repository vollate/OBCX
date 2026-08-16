## Context

OBCX currently starts both `ActorManager` and `PluginManager`, installs two
extension SDKs, accepts plugin and actor metadata, and retains both the
fixed-shard `asio-v1` scheduler and the native coroutine `native-v2`
scheduler. The archived native scheduler change deliberately kept these paths
for a compatibility window and withheld native-v2 default activation because
the balanced-load benchmark missed its rollout threshold.

The compatibility window now costs more than it protects: lifecycle,
configuration, dynamic loading, documentation, build tooling, tests, and
standalone repositories must all account for models that are no longer meant
for new development. The cutover affects the OBCX repository plus the bridge,
message-store, and registry repositories checked out under `local_plugin/`.

This design treats actor-only operation as a release gate, not an incremental
runtime compatibility feature. Retired inputs and binaries receive no parser,
detector, translator, warning path, adapter, or shim in the resulting system.

## Goals / Non-Goals

**Goals:**

- Make `IActorV2` and the native work-stealing scheduler the only extension
  ABI and actor execution engine.
- Remove all production plugin, V1 actor, `asio-v1`, and engine-selection
  code, including their public APIs, configuration, build tooling, and tests.
- Establish one actor package contract across metadata, SDK/CMake helpers,
  repository layout, and registry publication.
- Require the native scheduler to pass all recorded correctness and
  performance gates before the removal commit can complete.
- Coordinate core, bridge, message-store, and registry releases with
  reproducible cross-repository tests.

**Non-Goals:**

- Supporting, detecting, translating, or diagnosing retired plugin/V1 input
  formats in the actor-only release.
- Providing a dual-run period, compatibility shim, or in-process fallback.
- Replacing Boost.Asio as the network, timer, and I/O substrate behind actor
  interoperability.
- Turning the in-process actor runtime into a distributed actor system.
- Rewriting QQ/Telegram bridge business behavior unrelated to its extension
  boundary.

## Decisions

### 1. Use a gated hard cutover

The implementation is sequenced, but the shipped target has no compatibility
mode. Native-v2 must first pass the previously recorded balanced-load target
and every existing correctness, sanitizer, stress, shutdown, CPU, and memory
gate. Only then may the change remove the old runtime paths and become
releasable.

An immediate deletion was rejected because it would make a runtime with a
known failed rollout gate the only production engine. A two-release deprecation
was rejected because OBCX has already completed a compatibility window and the
goal is to stop maintaining parallel contracts.

### 2. Make V2 identity implicit in the only actor ABI

`ActorManager` retains explicit numeric ABI validation for actor libraries,
but only the supported V2 generation and V2 factory contract exist in public
headers or loader branches. `IActor`, V1 factory symbols,
`AsioActorV1Adapter`, mixed-version dispatch, and `allow_v1_actors` are
deleted. Actor-to-Asio suspension continues through `ActorContext::await_asio`
and never reintroduces Asio into the actor dispatch ABI.

Keeping a dormant V1 rejection branch was rejected: the dynamic loader should
look only for the current actor symbols and report ordinary actor loading
failure when they are absent.

### 3. Remove engine selection rather than changing its default

Runtime construction directly owns the native scheduler. Configuration no
longer contains an actor engine selector, and no `asio-v1` scheduler target or
rollback probe is built. Thread-budget, mailbox, metrics, backpressure, and
shutdown configuration remain because they govern native operation rather
than compatibility.

Leaving `engine = "native-v2"` as a one-value setting was rejected because it
preserves dead configuration and suggests that alternatives remain supported.

### 4. Delete legacy input surfaces without compatibility handling

The runtime and build system read only actor configuration and actor package
metadata. Plugin config accessors, `[plugins]` parsing, plugin manifests,
plugin dependency extraction, special legacy-key validation, migration
warnings, and automatic rewrites are deleted. Unknown or irrelevant TOML
content receives only whatever generic behavior the owning parser already
applies; there is no plugin-specific branch.

This deliberately favors a smaller, auditable contract over a friendlier
transition. A written breaking-change notice may show the supported actor
setup, but no shipped code understands retired inputs.

### 5. Treat standalone repositories as release-gated actor packages

The bridge exposes only its V2 actor entry point while retaining QQ/Telegram
Asio internals behind `await_asio`. Message-store builds only against the V2
actor SDK. `plugin-registry` becomes an actor registry with actor-only index
schema and validation. The checkout/build layout changes from `local_plugin/`
to `local_actor/`; package and repository names that advertise plugins are
renamed through their own repository release process.

Core removal cannot complete until installed-SDK builds and smoke tests pass
for bridge and message-store and registry generation accepts their actor
metadata.

The official plugin template is converted into the canonical actor template.
Other plugin-only repositories referenced by a developer's `plugins.toml` are
not silently migrated: the core build stops consuming those entries, and such
repositories must adopt the actor package contract independently if they are
to participate in a later release.

### 6. Prove absence as well as behavior

Tests cover the one supported runtime, package contract, and cross-repository
flows. A source/build audit asserts that retired public headers, targets,
symbols, config accessors, and runtime branches are absent. It does not feed
legacy inputs to compatibility diagnostics, because no such behavior is part
of the new contract.

## Risks / Trade-offs

- **[Native-v2 still misses a gate]** → Keep this change incomplete and do not
  remove or release the existing fallback until the failing workload is fixed
  and the recorded benchmark is reproducible.
- **[Atomic removal creates a large review surface]** → Land preparatory
  actor-only changes behind behavior-preserving commits, then perform one
  auditable removal phase after cross-repository readiness is proven.
- **[Independent repository releases drift]** → Pin tested revisions in the
  cross-repository harness and require a compatibility matrix for the cutover
  release.
- **[Users lose old binaries and manifests immediately]** → Publish a clear
  breaking release note and canonical actor examples; accept that old inputs
  do not run on the actor-only release.
- **[Renames disrupt packaging and automation]** → Make registry, repository,
  directory, CMake, and documentation renames explicit roadmap items with
  clean-environment verification.
- **[Removing runtime rollback raises operational risk]** → Finish load,
  soak, shutdown, and restart testing before release. Operational recovery is
  deployment rollback to the prior OBCX release, not a second engine inside
  the new binary.

## Migration Plan

1. Reproduce the archived V1/native-v2 baselines and fix native-v2 until every
   cutover threshold passes on the designated benchmark environment.
2. Convert bridge, message-store, their metadata, and the registry to the
   actor-only package contract; prove them against the installed SDK.
3. Simplify core startup and loading to V2/native-only behavior, then delete
   plugin, V1, Asio-v1, compatibility configuration, build targets, and tests.
4. Rename active directories and package/registry surfaces and update current
   documentation, examples, packaging, and CI.
5. Run the complete core and cross-repository verification matrix, perform a
   clean install/start/dispatch test, and release all coordinated artifacts.

Before step 3 completes, rollback means reverting the preparatory change while
the old release remains deployable. After release, rollback means deploying
the previous OBCX binary and its accepted configuration; the actor-only binary
contains no fallback engine or legacy loader.

## Open Questions

None. The cutover boundary, absence of compatibility handling, participating
repositories, and release gate have been explicitly selected.
