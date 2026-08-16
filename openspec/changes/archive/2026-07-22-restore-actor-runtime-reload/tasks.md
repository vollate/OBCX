## 1. Make Configuration Generation-Scoped

- [x] 1.1 Add tests for side-effect-free candidate parsing, immutable snapshots, normalized process-owned fingerprints, and secret-free diff diagnostics
- [x] 1.2 Refactor `ConfigLoader` parsing and typed accessors to produce immutable `RuntimeConfigSnapshot` objects without mutating the active snapshot
- [x] 1.3 Register a read-only generation configuration view in `ActorServices` and expose it through `ActorContext`
- [x] 1.4 Move bridge mappings and other in-tree actor configuration from process-global mutable state into actor instances backed by their generation snapshot
- [x] 1.5 Add actor SDK documentation and conformance fixtures forbidding reloadable actors from consulting mutable process-global configuration

## 2. Build Complete Candidate Generations

- [x] 2.1 Add builder tests proving startup, validation-only, and reload candidates share parse, contract, pipeline, dependency, construction, and activation outcomes
- [x] 2.2 Extract `RuntimeGeneration` and a common generation builder from `main.cpp`, preserving initial startup behavior and actor-only validation ordering while injecting the process-owned, startup-populated `BotRegistry` into every generation
- [x] 2.3 Implement generation-specific actor package staging that classifies process-owned versus actor-private dependencies, assigns content-versioned filenames and dynamic-link identities to private dependencies, rewrites and validates closure edges before `dlopen`, preserves relative layout, and cleans up failed candidates
- [x] 2.4 Compare candidate and active process-owned fingerprints and reject bot, database-instance, or resolved thread-budget changes with `reload_restart_required`
- [x] 2.5 Add tests proving actor entries, actor-owned sections, pipelines, routing policy, and rebuilt V2 binaries remain reloadable

## 3. Implement Transactional Cutover

- [x] 3.1 Add deterministic ingress-gate tests for messages admitted before closure, waiting during cutover, admitted after reopening, aborted cutover, and process shutdown
- [x] 3.2 Implement `ActorRuntimeReloadController` with atomic active-generation ownership, a single-flight state machine, and an asynchronous root-ingress gate
- [x] 3.3 Add generation route accounting that includes downstream emissions and terminal stages, plus a bounded non-blocking drain wait API
- [x] 3.4 Implement prepare, gate, drain, atomic swap, reopen, and retirement with candidate-only cleanup on every pre-publication failure
- [x] 3.5 Retain old actor objects, schedulers, services, DSO handles, and staging directories until all scheduled and suspended generation references are released
- [x] 3.6 Add forced-interleaving tests for reload versus Asio suspension/completion, terminal emission, drain timeout, a second reload, and shutdown

## 4. Restore The Operator Command And Observability

- [x] 4.1 Add command-handler tests for `reload` in TUI and `--no-tui` contexts and for immediate `reload_busy` handling
- [x] 4.2 Register an asynchronous reload callback in `CliHandler::Context` and route bot event callbacks through the reload controller instead of a fixed orchestrator
- [x] 4.3 Add attempt/generation ids, phase and drain timings, changed-domain summaries, stable failure codes, and reload counters without configuration values or message payloads
- [x] 4.4 Add validated reload drain-deadline configuration and document defaults, bounds, timeout behavior, and restart-required fields

## 5. Verify Actor-Only Reload End To End

- [x] 5.1 Add an end-to-end test that changes a bridge group mapping, reloads, keeps bot connections running, and proves post-cutover messages use only the new mapping
- [x] 5.2 Add an end-to-end test that stages a rebuilt bridge or message-store actor and a changed private dependency retaining its original SONAME, holds old actor work suspended, and proves the old generation executes the old dependency while the candidate executes its content-versioned image
- [x] 5.3 Run core unit, race, repeated reload, shutdown, ASan/UBSan, and TSan suites plus clean installed-SDK bridge/message-store reload tests
- [x] 5.4 Extend source/header/symbol audits to distinguish the valid V2 actor `reload` entry point from banned plugin lifecycle, plugin reload, loader, manifest, and compatibility surfaces
- [x] 5.5 Update operator, architecture, actor-authoring, and actor-only breaking-change documentation with transactional semantics, failure codes, and deployment rollback guidance
