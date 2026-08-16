## Why

Actor authors currently implement a second, string-based dispatcher inside every
`IActorV2::handle_message`: compare `MessageEnvelope.type`, decode JSON, select a
handler, and normalize its result. This is repetitive, easy to get out of sync
with pipeline configuration, and prevents OBCX from rejecting unsupported stage
inputs before any workers or ingress are started.

## What Changes

- **BREAKING**: Move the actor SDK and supported build toolchain directly to
  C++26 static reflection, initially GCC 16 with `-std=c++2c -freflection`; no
  C++20 fallback or Clang compatibility layer is provided.
- Add `ReflectedActor<Derived>`, which discovers public `handle` overloads at
  compile time. Actor authors declare only typed handlers returning
  `ActorResult` or `ActorTask<ActorResult>`; they do not register message types,
  write dispatch tables, or override `handle_message`.
- Derive the wire message type from the fully qualified C++ type name using
  standard reflection, decode the payload with existing nlohmann JSON ADL
  conversions, and provide typed `ActorResult::emit` helpers that derive the
  same identity and preserve routing metadata.
- **BREAKING**: Require every actor library to export an automatically generated,
  versioned input contract through `obcx_get_actor_contract`. Actor libraries
  without the contract are rejected; no legacy message-type aliases or binary
  compatibility path is added.
- Reorder startup so enabled actor libraries and their input contracts are
  loaded before actor-aware pipeline validation. Add
  `obcx --validate-config <config>` for the same load-and-validate path without
  starting workers, services, bots, or ingress.
- Validate only facts the runtime actually knows: actor availability, declared
  input support, stage/dependency integrity, explicit `after` acyclicity, and
  existing scheduler/database/service rules. Pipeline `output` remains an
  orchestration declaration and is not treated as a statically provable control
  flow graph.
- Replace silent routing-depth loss with runtime protection for routes that
  actually execute: an ancestor trace detects repeated
  `(pipeline, stage, message_type)` nodes and a configurable/default hop limit
  terminates unbounded non-repeating routes with structured diagnostics.
- Migrate the in-tree template, message-store actor, bridge actor, tests,
  packaging, and installed SDK to the reflected authoring contract.

## Capabilities

### New Capabilities

- `reflected-actor-dispatch`: C++26 handler discovery, canonical message
  identity, JSON payload dispatch, sync/async return normalization, typed emit,
  and author-facing diagnostics.

### Modified Capabilities

- `actor-abi-v2`: Require a versioned, generated actor input-contract symbol and
  reject old or malformed actor libraries during loading.
- `actor-runtime-operations`: Make startup and validation actor-aware, add the
  validation-only CLI path, and report actual routing cycles/hop-limit failures
  without attempting global message-flow analysis.

## Impact

- Public actor headers (`IActorV2`, `ActorResult`, export helpers, and the new
  reflected base), ActorManager loading, orchestrator routing, configuration
  validation, CLI startup, and runtime diagnostics.
- Project and installed-SDK CMake requirements, compiler selection, Nix/CI
  images, package metadata, release targets, and supported platforms.
- All actor shared libraries must be rebuilt atomically against the new SDK and
  export their generated input contract; existing actor binaries will not load.
- Actor authors keep their existing nlohmann `to_json`/`from_json` definitions;
  this change does not introduce a general reflection-based serializer.
