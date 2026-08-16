## Context

`IActorV2` currently exposes one untyped entry point:
`ActorTask<ActorResult> handle_message(const MessageEnvelope&, ActorContext&)`.
Each actor repeats the same secondary-dispatch code: inspect a string, decode
JSON into the corresponding C++ type, call business logic, and convert the
return value. The string and handler can drift independently, and the runtime
cannot learn an actor's accepted inputs before invoking it.

The project has no deployed legacy protocol to preserve, so this design favors
one generated contract over compatibility shims. It also deliberately stops at
locally knowable facts. Proving that an arbitrary emitted message can reach a
compatible handler through every business branch would require an output/dataflow
IR, not merely stronger actor types.

C++26 static reflection supplies the missing language mechanism. P2996 provides
member enumeration, exact reflected overload splicing, `identifier_of`, and
`parent_of`; P3096 provides function parameter and return-type reflection. The
canonical wire name must not use `display_string_of`, whose representation is
implementation-defined. The initial supported compiler is GCC 16 with
`-freflection`; Clang is excluded until its official reflection implementation
passes the same conformance suite.

## Goals / Non-Goals

**Goals:**

- Let actor authors express message handling as ordinary typed `handle`
  overloads with no registration table, macros per message, or manual
  `handle_message` implementation.
- Generate one canonical, fully qualified wire identity from each C++ message
  type and use it consistently for receive, emit, contracts, and configuration.
- Detect malformed handlers at compile time and unsupported configured actor
  inputs before the runtime starts workers or ingress.
- Preserve synchronous and `ActorTask` handlers, existing nlohmann JSON ADL
  conversions, mailbox semantics, and routing metadata.
- Detect cycles in the explicit stage dependency graph at validation time and
  stop/report message-routing loops that actually execute at runtime.

**Non-Goals:**

- Inferring actor output sets or storing input/output message pairs.
- Proving global pipeline reachability, business-branch feasibility, or static
  message-flow acyclicity.
- Replacing nlohmann JSON with a general reflection serializer.
- Preserving C++20, Clang, old actor binaries, short type names, aliases, or an
  old wire protocol.
- Reimplementing CAF-style behavior matching, distributed actors, or actor
  supervision.

## Decisions

### 1. Make C++26 reflection a hard SDK baseline

Core, actor libraries, the installed SDK, examples, and actor package builds
will use CMake's C++26 mode and the initial GCC 16 reflection flags. Configure
checks will require `__cpp_impl_reflection >= 202506L` and fail with a focused
diagnostic when the compiler cannot compile the reflection probes. The first
supported release targets Linux x86_64 and arm64; other compiler/platform
combinations remain unsupported until they pass the probes and installed-SDK
tests.

This is preferred to maintaining a C++20 registration fallback because two
authoring models would preserve the duplicate metadata and greatly enlarge the
SDK test matrix. `magic_enum` cannot enumerate member functions or inspect
their parameter and return types, so it cannot remove handler registration.

The normative language/tooling references are:

- WG21 P2996R13: <https://www.open-std.org/jtc1/SC22/wg21/docs/papers/2025/p2996r13.html>
- WG21 P3096R12: <https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3096r12.html>
- GCC C++ status: <https://gcc.gnu.org/projects/cxx-status.html>
- Clang C++ status: <https://clang.llvm.org/cxx_status>
- CMake `CXX_STANDARD`: <https://cmake.org/cmake/help/latest/prop_tgt/CXX_STANDARD.html>

### 2. Discover a constrained set of ordinary overloads

The SDK adds a CRTP base:

```cpp
class BridgeActor final : public ReflectedActor<BridgeActor> {
public:
  ActorTask<ActorResult> handle(
      const obcx::message_store::events::MessageStored& message,
      const MessageEnvelope& envelope,
      ActorContext& context);

  ActorResult handle(
      const bridge::events::RetryRequested& message,
      const MessageEnvelope& envelope,
      ActorContext& context);
};
```

`ReflectedActor<Derived>` implements `IActorV2::handle_message` as `final`. At
compile time it enumerates public, direct members of `Derived` named `handle`
and retains only candidates with this exact logical signature:

```text
(const Message&, const MessageEnvelope&, ActorContext&)
  -> ActorResult | ActorTask<ActorResult>
```

The first parameter supplies the accepted message type after alias removal.
The implementation invokes the exact reflected overload via splicing, avoiding
name-based overload resolution after discovery. It emits focused compile-time
errors for no valid handlers, non-public or malformed `handle` declarations,
duplicate normalized message inputs, unsupported return types, unnamed/local
message types, and missing JSON conversions.

Inherited handler discovery and permissive signature conversions are excluded:
they make ownership and diagnostics ambiguous. Shared logic can still live in
ordinary helper functions or be called by a direct forwarding overload.

### 3. Derive the wire ID from named lexical scopes

The canonical ID is the C++ message's full named scope without a leading
`::`, for example:

```text
obcx::message_store::events::MessageStored
```

Generation first dealsiases the parameter type, then walks `parent_of` and uses
`identifier_of` for the message and every named enclosing namespace/class,
joining components with `::`. A message must be a named, nonlocal class or enum
and every contributing scope must be nameable. Anonymous namespaces, local
types, lambdas, and implementation-generated display text are rejected.

The same consteval implementation supplies receive dispatch, typed emit, and
the exported actor contract. Moving or renaming the type is therefore an
intentional protocol change. No explicit binding, short-name registry, hash, or
legacy alias is supported.

### 4. Keep JSON at the boundary and normalize handler execution

For a matched input, the generated dispatcher uses the existing nlohmann ADL
conversion to decode `envelope.payload` into the exact message type. A sync
handler result is lifted into the normal `ActorTask<ActorResult>` path; an async
result is returned without an extra business-level wrapper.

The decoded message and any dispatch state are owned by an opaque lifetime
anchor retained through the root actor task, so a coroutine handler's
`const Message&` remains valid across suspension. Decoding and handler-task
construction happen as scheduled actor work rather than while scheduler or
mailbox locks are held. If the current scheduler constructs the task before a
worker claim, the dispatch task must be lazy so decoding begins on first worker
resume.

Unknown IDs produce `unsupported_message_type`; JSON conversion failures
produce `invalid_message_payload`. Exceptions after handler entry continue to
use the existing `actor_exception` path. Failures include actor and routing
identity but never the complete payload.

### 5. Add typed emit without making `ActorResult` a type-level graph

`ActorResult` remains non-template. It gains a typed helper conceptually
equivalent to:

```cpp
result.emit(message, parent_envelope, options);
```

The helper derives the full canonical ID, serializes with nlohmann JSON, and
inherits required correlation/routing metadata from the parent envelope while
allowing explicit routing options. The low-level
`emit(MessageEnvelope)` path remains available for infrastructure and dynamic
adapters.

`TypedResult<Outputs...>` and input/output-pair declarations were rejected.
They appear to make pipeline checking stronger, but branching, filtering,
dynamic emission, fan-out, and feedback routes quickly require a control-flow
and dataflow representation. The runtime will not pretend that a declared set
proves an executable path.

### 6. Export the generated input contract in the V2 dynamic-library ABI

`OBCX_ACTOR_EXPORT_V2(ActorType, ...)` will also export:

```cpp
extern "C" const char* obcx_get_actor_contract();
```

The returned pointer has static lifetime and contains deterministic JSON with
at least:

```json
{
  "schema_version": 1,
  "actor": "bridge",
  "accepted_inputs": [
    "obcx::message_store::events::MessageStored",
    "bridge::events::RetryRequested"
  ]
}
```

The accepted inputs are generated from the same reflected handler set used for
dispatch, so authors cannot manually drift the manifest from the code. Outputs
are intentionally absent. ActorManager validates the ABI generation, required
symbols, schema, actor identity, canonical input uniqueness, and JSON shape,
then stores the parsed contract beside the loaded actor. A missing, malformed,
or unsupported contract rejects the library cleanly before registration.

Adding this required symbol is an atomic ABI cutover. Every actor package is
rebuilt; no compatibility loader branch is retained.

### 7. Validate with loaded actor facts before creating runtime activity

Startup is reordered to:

```text
parse config
  -> validate syntax and non-actor references
  -> load enabled actor libraries and contracts
  -> validate actor-aware pipelines
  -> create scheduler and services
  -> register actors
  -> start bots and ingress
```

Actor-aware validation checks that each referenced actor is enabled/loadable
and that the stage input appears in its accepted-input contract. It also keeps
the existing unique-stage, `after` reference, explicit dependency-cycle,
scheduler, database, and service checks.

The `output` field continues to describe orchestration intent, including the
runtime's terminal/downstream decisions. It is not matched against an inferred
actor output contract and is not used to construct a static dataflow proof.

`obcx --validate-config <config>` executes the same parse, load, contract, and
pipeline validation path, then unloads cleanly without constructing actor
workers, starting services/bots, or opening ingress. This makes configuration
mistakes observable in CI and image validation.

### 8. Separate structural cycles from executed routing loops

The existing explicit `after` dependency graph remains a startup-time DAG
check. Message-flow cycles are not statically inferred.

Each executing route carries an internal context with a hop count and an
ancestor sequence of `(pipeline, stage, message_type)`. Entering a node already
present in that route's ancestors terminates that branch with
`message_routing_cycle` and a compact trace. Exceeding the hop limit (default
32) terminates it with `message_routing_hop_limit`. The current silent
`depth > 32` return is removed.

Fan-out children receive separate trace snapshots so one sibling cannot create
a false cycle in another. Terminal asynchronous continuations retain their
route context across suspension. The trace is internal metadata and is not
added to the public wire payload.

This mechanism reports loops that really occur without claiming to prove every
possible branch. Repetition of message types alone is insufficient because a
valid route may use the same type at different stages.

### 9. Use stable failure codes and bounded diagnostics

New failures use stable codes:

- `unsupported_message_type`
- `invalid_message_payload`
- `message_routing_cycle`
- `message_routing_hop_limit`

Diagnostics include the available actor, pipeline, stage, canonical message
type, message/correlation identifier, and bounded route trace. Full payloads
and decoded message contents are excluded by default.

## Risks / Trade-offs

- **[GCC reflection implementation changes before final C++26 release]** → Pin
  the admitted GCC 16 revision/range, gate it with focused compile probes, and
  keep reflection code behind a small SDK header surface.
- **[Compiler diagnostics from reflection become unreadable]** → Validate each
  handler property separately and cover negative `try_compile` cases with
  expected diagnostic anchors.
- **[Fully qualified names couple protocol identity to source layout]** → Treat
  namespace/type renames as explicit protocol changes and test exact canonical
  strings. This coupling is chosen instead of a second manual identity registry.
- **[Async handler references outlive decoded storage]** → Retain decoded
  storage through the root task and add a suspension/resumption lifetime test
  under sanitizers.
- **[Actor loading during validation runs library initialization]** → Require
  export/contract discovery to be side-effect-free, construct no actor instance
  for validation, and unload all handles on every failure path.
- **[Runtime trace adds per-route allocations]** → Use compact bounded storage
  or copy-on-write snapshots and benchmark fan-out; correctness and diagnostic
  clarity take priority over silently dropping loops.
- **[Local validation is mistaken for global correctness]** → Document the
  exact checked facts and add tests showing that `output` does not create a
  static reachability claim.

## Migration Plan

1. Pin the GCC 16/CMake toolchain, add positive and negative reflection probes,
   and move core plus installed actor SDK targets to C++26.
2. Implement canonical IDs, `ReflectedActor`, JSON dispatch, return
   normalization, async lifetime ownership, and typed emit behind focused unit
   and compile tests.
3. Extend the V2 export helper and ActorManager with the generated input
   contract, then make the symbol mandatory.
4. Reorder startup, add validation-only execution, and enforce actor input
   support after libraries are loaded but before runtime activity begins.
5. Replace the silent routing-depth guard with branch-local cycle/hop tracing
   and structured failures.
6. Migrate all in-tree and standalone actor packages in one coordinated SDK
   release; update templates, CI, Nix, packaging, and documentation.
7. Run clean installed-SDK builds and end-to-end validation/routing tests on
   Linux x86_64 and arm64 before release.

Rollback is deployment of the preceding OBCX release together with actor
binaries built for that release. The new runtime contains no C++20 or old-ABI
fallback.

## Open Questions

None. The compiler cutover, canonical identity, contract boundary, validation
scope, and decision not to build a static message-flow IR are explicit.
