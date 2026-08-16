## 1. Admit The C++26 Toolchain

- [x] 1.1 Add a configure-time GCC 16 static-reflection probe requiring `__cpp_impl_reflection >= 202506L` and verify the failure diagnostic on unsupported compilers
- [x] 1.2 Move core, tests, installed `OBCXActor` SDK targets, examples, and standalone actor builds to C++26 with the admitted reflection flags
- [x] 1.3 Update Nix, CI, container, package metadata, and Linux x86_64/arm64 release jobs to use the pinned admitted GCC 16 toolchain
- [x] 1.4 Add clean installed-SDK configure/build tests that prove no hidden source-tree dependency or C++20 fallback remains

## 2. Build Canonical Reflected Message Identity

- [x] 2.1 Add positive compile tests for namespace/class parent walking, alias removal, and exact fully qualified canonical IDs
- [x] 2.2 Add negative compile tests for local, anonymous, unnamed-scope, and otherwise unstably nameable message types
- [x] 2.3 Implement the consteval `identifier_of`/`parent_of` canonical-name utility without using `display_string_of`
- [x] 2.4 Use one canonical-name implementation in receive dispatch, typed emit, and actor contract generation

## 3. Implement Reflected Handler Discovery

- [x] 3.1 Add `try_compile` fixtures for valid sync/async overload sets and for no-handler, non-public, wrong-arity, wrong-cvref, wrong-context, bad-return, and duplicate-normalized-input failures
- [x] 3.2 Implement public direct `handle` member enumeration and exact logical signature validation in `ReflectedActor<Derived>`
- [x] 3.3 Generate exact overload invocation through reflection splicing and make the base implementation of `handle_message` final
- [x] 3.4 Add stable diagnostic anchors for every rejected handler property and assert them in negative compile tests

## 4. Implement Dispatch, Lifetime, And Typed Emit

- [x] 4.1 Add unit tests for exact type dispatch, nlohmann ADL decode, sync-result normalization, unsupported input, malformed JSON, and missing conversion compile failures
- [x] 4.2 Implement generated JSON dispatch and stable `unsupported_message_type` and `invalid_message_payload` failures without payload logging
- [x] 4.3 Add a sanitizer-backed test that suspends an async handler and reads its decoded `const Message&` after resumption
- [x] 4.4 Retain decoded storage through the root actor task and ensure decode/task construction begins only as scheduler-owned actor work outside scheduler and mailbox locks
- [x] 4.5 Add typed `ActorResult::emit` tests for canonical identity, JSON serialization, parent metadata inheritance, explicit routing options, and low-level envelope emission
- [x] 4.6 Implement typed emit while keeping `ActorResult` non-template and preserving the infrastructure `MessageEnvelope` path

## 5. Add The V2 Actor Input Contract

- [x] 5.1 Define and test the schema-version-1 JSON contract containing actor identity and unique canonical accepted inputs only
- [x] 5.2 Extend `OBCX_ACTOR_EXPORT_V2` to generate the static-lifetime `obcx_get_actor_contract` symbol from the reflected handler set
- [x] 5.3 Add ActorManager loader tests for a valid contract, missing symbol, null/invalid JSON, unsupported schema, actor-name mismatch, duplicate input, and cleanup after rejection
- [x] 5.4 Load, validate, and store actor input contracts before construction/registration, with no old-binary compatibility branch

## 6. Make Configuration Validation Actor-Aware

- [x] 6.1 Refactor startup into explicit parse, non-actor validation, actor contract loading, actor-aware validation, runtime creation, registration, and ingress phases
- [x] 6.2 Add tests proving an unavailable actor or unsupported stage input fails before workers, services, bots, or ingress start
- [x] 6.3 Validate unique stage names, existing `after` references, explicit dependency acyclicity, actor inputs, and existing scheduler/database/service rules with actionable paths
- [x] 6.4 Add regression tests proving pipeline `output` does not trigger inferred output-set, input/output-pair, reachability, or business-branch analysis
- [x] 6.5 Implement `obcx --validate-config <config>` using the production validation path and test success/failure plus absence of worker, service, bot, and ingress startup

## 7. Protect Executed Message Routes

- [x] 7.1 Add routing tests for an actual repeated ancestor, a non-repeating route over 32 hops, a route completing at the limit, fan-out sibling isolation, and terminal async propagation
- [x] 7.2 Replace the silent depth guard with branch-local hop and `(pipeline, stage, message_type)` ancestor context
- [x] 7.3 Emit `message_routing_cycle` and `message_routing_hop_limit` failures with bounded traces and no complete payloads
- [x] 7.4 Benchmark route-context overhead under fan-out and use bounded or copy-on-write storage if required without weakening branch isolation

## 8. Migrate Actors And Package Surfaces

- [x] 8.1 Convert the official actor template and fixture actors to typed `handle` overloads with generated contracts and no manual secondary dispatch
- [x] 8.2 Convert message-store and bridge actors, preserving their sync/async behavior, emitted metadata, failure semantics, and Asio interoperability
- [x] 8.3 Update actor package schemas, examples, author documentation, configuration references, and breaking release notes for full canonical message names and the required input contract
- [x] 8.4 Remove obsolete hand-written type switches, registration helpers, short-name constants, and compatibility code after all packages migrate

## 9. Verify The Atomic Cutover

- [x] 9.1 Run core unit, compile-fail, integration, routing, scheduler, ASan/UBSan, and TSan suites on the admitted x86_64 toolchain
- [x] 9.2 Run clean installed-SDK builds and end-to-end message-store-to-bridge pipeline validation/dispatch tests
- [x] 9.3 Run the supported arm64 build and reflection conformance suite and record the compiler/toolchain evidence
- [x] 9.4 Audit installed headers, symbols, packages, and documentation to confirm the C++26 reflected contract is the only actor authoring path and old actor libraries are rejected cleanly
