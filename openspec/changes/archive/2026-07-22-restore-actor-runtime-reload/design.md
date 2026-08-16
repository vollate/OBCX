## Context

The actor-only cutover removed the old `PluginManager`-based reload operation.
That removal was correct for the retired extension model, but the current
`CliHandler` now recognizes only exit/quit and log-level changes. The surviving
`ConfigLoader::reload_config()` reparses a mutable singleton and has no caller;
invoking it alone would not rebuild actors, replace routing, or establish a
safe boundary for work already executing.

Startup currently assembles one `ActorRuntimeBundle` in `main.cpp`, and each bot
event callback captures that bundle's orchestrator directly. Actor libraries
are loaded by pathname and remain live through `SafeActorWrapper`. The native
scheduler can drain or cancel at shutdown, but there is no resumable root
ingress gate, bounded drain primitive, runtime generation owner, or staged DSO
identity. Bridge also reads the process-global `ConfigLoader` in its actor
constructor and stores mappings in global mutable state, which prevents old and
candidate actor generations from coexisting safely.

Reload therefore has to be a new actor-runtime lifecycle, not the restoration
of the deleted plugin callback sequence.

## Goals / Non-Goals

**Goals:**

- Apply actor-owned configuration and rebuilt V2 actor libraries without
  restarting bot connections.
- Validate and construct a complete candidate while the current generation
  remains available.
- Establish one deterministic ingress boundary so accepted work executes
  exactly once on either the old or new generation.
- Keep old configuration, actor objects, schedulers, services, and DSO handles
  alive until all code references have retired.
- Abort cleanly on preparation or drain failure and provide actionable,
  secret-safe operational diagnostics.

**Non-Goals:**

- Restoring plugins, plugin manifests, plugin lifecycle callbacks, or any
  compatibility loader.
- Hot-reconfiguring bot connections, database instances, process thread pools,
  or other process-owned resources in the initial version.
- Migrating arbitrary in-memory actor state between generations. State that
  must survive reload belongs in configured persistent services.
- Supporting multiple simultaneous reload transactions or per-message mixed
  configuration within one runtime generation.

## Decisions

### 1. Put a reload controller between bot ingress and runtime generations

Introduce a process-owned `ActorRuntimeReloadController`. It owns an atomic
`shared_ptr<RuntimeGeneration>` for the active generation, an asynchronous root
ingress gate, and a single-flight reload state machine. Bot callbacks capture
the controller and call `process()` through it instead of capturing a fixed
orchestrator.

A `RuntimeGeneration` owns its immutable config snapshot, `ActorManager`, actor
I/O pool, scheduler, orchestrator, actor-scoped services, and generation id. It
holds references to explicitly process-owned services but does not create or
own their live state. In particular, the application owns the one populated
`BotRegistry` used by startup and every candidate generation.

Every in-flight root route holds a generation reference until all awaited and
terminal descendants finish. This makes lifetime explicit and lets ordinary
reference ownership protect actor code after the active pointer changes.

Changing routing tables in the existing orchestrator was rejected. It cannot
atomically replace actor objects, scheduler mailboxes, actor-owned state, and
DSO lifetimes, and it would let old work observe new configuration.

### 2. Replace mutable singleton reload with immutable snapshots

Refactor configuration loading into a side-effect-free parse/build operation
that returns `shared_ptr<const RuntimeConfigSnapshot>`. Startup and reload use
the same typed extraction and validation functions. The process may publish
the successful snapshot for operator inspection, but actors do not consult a
mutable process-global TOML table.

Each generation registers a read-only actor configuration view in
`ActorServices`. Actor code reads that view through `ActorContext` and stores
derived data on the actor instance. Bridge moves `GROUP_MAP` and related
derived configuration from global state into the `BridgeActor` generation and
initializes it from the context-backed snapshot before handling messages.

Temporarily swapping the singleton around actor construction was rejected. An
old coroutine or background callback could read the candidate accidentally,
and constructor failure could expose a partially reverted global state.

### 3. Reuse startup preparation for reload candidates

Extract the existing startup sequence—parse, non-actor validation, actor DSO
discovery, contract validation, pipeline validation, dependency ordering,
actor construction, service registration, and activation—into a builder that
returns either a ready, non-ingressing `RuntimeGeneration` or structured
failure details. `--validate-config`, initial startup, and reload share the
same validation core; only startup/reload continue into activation as needed.

The application constructs process-owned services before the first generation
and passes them to the builder through explicit inputs. The builder registers
the supplied live `BotRegistry` in each generation's `ActorServices`; it never
allocates a replacement registry or re-registers bots during reload. Candidate
readiness verifies that configured bot identities resolve in the supplied
registry without mutating it.

Actor construction must remain side-effect-free outside resources owned by the
candidate generation. External work begins only after publication and ingress.
This is preferred to validating one path and building through a second path,
which would retain the current opportunity for validation/activation drift.

### 4. Give every staged private dependency a generation-safe identity

Before discovery, resolve each enabled actor package and copy its loadable
closure into a generation-specific staging directory while preserving relative
library layout. ActorManager opens the staged main library, records its content
identity and origin, and transfers the handle into the generation-owned safe
wrapper. A candidate never calls `dlopen` using the active generation's loaded
pathname.

A unique pathname for the main DSO is not sufficient. Before opening any
candidate handle, the stager classifies each dependency as process-owned or
actor-private and records its content digest and dynamic-link identity. A
process-owned ABI dependency, including the supported OBCX runtime/SDK
libraries, may be reused only with the exact active content identity. Every
actor-private shared dependency is assigned a content-versioned filename and
dynamic-link identity, and every edge in the staged closure is rewritten to
reference that identity. On ELF this includes both the dependency's
`DT_SONAME` and its consumers' `DT_NEEDED` entries; other supported platforms
use the equivalent install-name mechanism. Identical private content may share
one identity, while different content must never retain the same identity.

The stager performs this transformation and validates the complete rewritten
closure before `ActorManager` calls `dlopen`. If the platform backend cannot
establish or verify unique identities, candidate preparation fails with
`reload_dependency_identity_conflict` while the active generation remains
untouched. Merely copying an unmodified `$ORIGIN` tree to a new directory is
explicitly invalid because the loader may reuse an active dependency with the
same SONAME.

Retired staging directories are removed only after their generation and all
actor aliases are destroyed. Failed candidates remove only their own staging
directory. Packaging and tests must prove that `$ORIGIN`-relative dependencies
still resolve from staging. A two-generation test rebuilds an actor-private
dependency with different behavior but the same original SONAME and proves
that the active actor continues to execute the old dependency while the
candidate executes the content-versioned replacement.

Unloading the active DSO before loading the replacement was rejected because a
suspended coroutine may still execute old code and because activation failure
would leave no reliable rollback target.

### 5. Classify process-owned changes before cutover

The reload builder compares a normalized process-owned fingerprint from the
candidate with the active snapshot. It includes enabled bot identities and
their complete connection/proxy/auth settings, database instance definitions,
and the resolved actor/I/O/blocking thread budget. Any difference produces a
`reload_restart_required` result before the ingress gate closes.

Actor entries, actor-owned tables (including top-level sections consumed by an
actor), pipelines, and routing policy remain reloadable. Reload is all-or-
nothing; it never silently retains old values for immutable domains while
publishing new actor values.

Rebuilding bots and databases inside the first implementation was rejected
because their independent event loops, sockets, polling offsets, database
handles, and blocking scheduler are process-owned and require a broader
application-generation design.

`BotRegistry` follows the same process-owned boundary but is intentionally
shared with all actor generations: it is the stable lookup surface for those
unchanged live bot connections. The process owns and populates it as bots
start, generations only hold a service reference, and reload never clears,
clones, or replaces it. This differs from actor-scoped services and derived
actor configuration, which are rebuilt for every candidate.

### 6. Use a prepare, gate, drain, swap, retire transaction

The reload transaction is:

1. Acquire the single-flight reload slot and assign an attempt id.
2. Parse, classify, stage, validate, construct, and activate a non-ingressing
   candidate while the active generation continues running.
3. Close the controller's root-ingress gate. Calls arriving after closure wait
   at the gate and have not yet selected a generation.
4. Wait for every route already admitted to the old generation, including
   downstream emissions and detached terminal stages, to reach a terminal
   state before the configured deadline. Internal scheduling remains enabled
   during this drain.
5. If drain times out, reopen the gate on the old generation and discard the
   candidate. Otherwise atomically exchange the active generation pointer.
6. Open the gate so waiting and future ingress captures the new generation,
   then retire the drained generation and its staging directory.

All fallible candidate work occurs before the exchange. After a successful
drain, publication is one no-throw pointer exchange; there is no partially
published rollback state. Process shutdown wins the state-machine race,
cancels a candidate, releases gated ingress through shutdown errors, and then
uses the existing bounded process shutdown path.

Immediately cancelling the old scheduler was rejected because accepted bot
messages would fail solely due to an administrative reload. Keeping ingress
open during drain was rejected because the old generation might never become
idle.

### 7. Add bounded, observable drain primitives

Add explicit route-admission accounting at the controller/generation boundary
and include orchestrator terminal tasks in the count. The scheduler exposes a
non-blocking quiescence observation/wait API rather than calling its current
unbounded `shutdown(Drain)` from the command path. Tests use forced gates and
manual completions, not timing alone, to prove the admission boundary, drain,
timeout, and shutdown interleavings.

The default drain deadline is configurable under actor-runtime reload settings
and is validated within a bounded range. Timing out aborts reload; it does not
cancel the live generation. Once drained and detached, normal generation
destruction can use drain shutdown with no pending work.

### 8. Keep command handling asynchronous and single-flight

`CliHandler::Context` receives a reload callback that returns an accepted/busy
result immediately. The controller performs reload outside the TUI/input
thread and publishes phase and completion output through the existing logger,
so command input and rendering do not freeze during validation or drain.

Attempt ids increase monotonically. Logs and metrics include attempt and
generation ids, phase durations, changed domain names, and stable codes such
as `reload_busy`, `reload_parse_failed`, `reload_contract_invalid`,
`reload_activation_failed`, `reload_dependency_identity_conflict`,
`reload_restart_required`, and `reload_drain_timeout`. Values, payloads,
credentials, and tokens are never logged.

### 9. Keep the lifecycle exclusively V2 actor-based

The new controller depends only on `ActorManager`, V2 contracts, the native
scheduler, and actor pipelines. The existing actor-only absence audit is
extended to distinguish the valid `reload` command from banned plugin reload
symbols and types. No legacy input receives special parsing or diagnostics.

## Risks / Trade-offs

- **[A non-cooperative actor never drains]** → Bound the wait, abort reload,
  reopen ingress on the still-live generation, and report the actor/mailbox
  context without payload contents.
- **[Actor package staging breaks relative dependencies]** → Stage the package
  closure with its relative layout and content-versioned private dependency
  identities, then add clean installed-package tests for every supported
  platform.
- **[A staged dependency reuses an active linker identity]** → Rewrite and
  validate private dependency names and dependency edges before `dlopen`, fail
  candidate preparation when uniqueness cannot be proved, and test rebuilt
  same-SONAME dependencies with both generations resident.
- **[Actors retain process-global mutable configuration]** → Migrate in-tree
  actors first, add an SDK rule and conformance fixture, and prevent reload
  readiness until actor configuration is generation-scoped.
- **[Frequent reloads temporarily double runtime resources]** → Allow only one
  candidate, expose staging/resource metrics, and destroy failed or retired
  generations promptly.
- **[Volatile actor caches reset]** → Document reconstruction semantics and use
  database-backed actor services for state that must survive reload.
- **[Queued ingress increases memory during a long drain]** → Bound the drain
  deadline and reuse normal ingress backpressure/cancellation when callers or
  process shutdown stop waiting.

## Migration Plan

1. Introduce immutable configuration snapshots and move bridge/message-store
   actor configuration off mutable globals without changing startup behavior.
2. Extract the common generation builder and make startup and
   `--validate-config` use it; add generation-specific actor package staging.
3. Add reload controller, ingress gate, deterministic drain accounting, and
   atomic generation publication behind tests while leaving the command
   unregistered.
4. Register `reload` in both command interfaces, add diagnostics/metrics, and
   document the restart-required fields and actor reconstruction semantics.
5. Run core race/sanitizer/shutdown tests plus installed bridge and
   message-store end-to-end reload tests before enabling the feature in release
   builds.

During development, rollback is removal of the unregistered controller path.
After release, a failed reload automatically retains the active generation;
deployment rollback remains replacement with the preceding OBCX release.

## Open Questions

None. The initial mutable/restart-required boundary and the global transactional
reload semantics are selected explicitly.
