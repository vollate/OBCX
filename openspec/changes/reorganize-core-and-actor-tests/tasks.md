## 1. Establish ownership and registration boundaries

- [x] 1.1 Inventory every root test, fixture, include, and CMake dependency that references Bridge or Message Store behavior, and record an actor-repository destination or generic core replacement for each item.
- [x] 1.2 Add package-scoped Bridge and Message Store test options that default on only for top-level standalone builds and support an explicit override.
- [x] 1.3 Add configuration/inventory checks proving root testing builds embedded actor artifacts without registering actor-owned cases, while standalone actor builds register their complete suites.

## 2. Implement deterministic WebSocket write completion

- [x] 2.1 Introduce a non-installed internal FIFO write-queue primitive with bounded asynchronous admission and Asio-native per-request completion.
- [x] 2.2 Migrate `WebsocketClient::send` and its writer lifecycle from polled `std::future` completion to the queue primitive without changing the public API or configured capacity.
- [x] 2.3 Ensure write failure, channel closure, and client shutdown complete the active, queued, and backpressured senders exactly once without logging complete payloads.
- [x] 2.4 Add a manually controlled write operation for tests and deterministic cases for non-overlap, FIFO ordering, queue saturation, per-request success/failure, and shutdown cleanup.
- [x] 2.5 Replace fixed startup/connection sleeps in the Beast loopback smoke with explicit listening, connected, message, and teardown completion signals.

## 3. Implement deterministic OneBot action deadlines

- [x] 3.1 Refactor pending OneBot actions around one atomic terminal transition for response, timeout, transport failure, and cancellation, with one authoritative removal path.
- [x] 3.2 Add a non-installed deadline factory whose production implementation uses Asio steady timers and whose test implementation exposes manually fired/disarmed deadlines.
- [x] 3.3 Add deterministic tests for matching response, explicit timeout, both response/timeout orderings, unknown late response, send failure, disconnect, destruction, and empty pending-state cleanup.
- [x] 3.4 Remove the sequential `WeakNetworkWrites` scenario and superseded wall-clock timeout/delayed-response assertions after equivalent deterministic coverage passes.
- [x] 3.5 Register deterministic WebSocket queue, action deadline, and bounded loopback smoke tests in the normal root test gate without requiring the experimental network option.

## 4. Move actor behavior to actor repositories

- [x] 4.1 Move the real Message Store to Bridge pipeline smoke and its Bridge-private headers, fixtures, forwarding assertions, and installed artifact wiring into the Bridge test suite.
- [x] 4.2 Move bot-facing Bridge reload smoke behavior into the Bridge repository, preserving ingress, mapping, cutover, old-route retirement, and no-bot-reconnect assertions.
- [x] 4.3 Move the real unmatched-command Message Store/Bridge pipeline regression and any remaining forwarding/mapping/media/retry assertions out of root runtime tests into Bridge-owned tests.
- [x] 4.4 Keep Message Store schema, persistence, identity, deduplication, and emitted-message tests in the Message Store repository and add any coverage displaced from root there.
- [x] 4.5 Replace rebuilt Message Store and other production-actor dependencies in root staging, generation, and reload tests with generic same-SONAME lifecycle/dependency fixtures.
- [x] 4.6 Remove root test includes, compile definitions, targets, and sources that depend on actor-private headers or production actor implementation files, while retaining generic actor ABI/SDK fixtures.

## 5. Rework conformance and CI tiers

- [x] 5.1 Update cross-repository conformance to configure Bridge and Message Store as top-level projects, run each actor-owned suite once against the installed SDK, and preserve repository-qualified failure output.
- [x] 5.2 Keep the generic root installed-SDK consumer and installed-header allowlist independent of production actors, and verify no internal WebSocket test seam is installed.
- [x] 5.3 Define stable `fast`, `full`, and `conformance` labels or presets with deterministic WebSocket coverage in fast/full and clean actor builds only in conformance.
- [x] 5.4 Update supported-platform CI to run the full core tier and the standalone actor conformance tier without duplicate embedded actor cases.
- [x] 5.5 Add an automated inventory assertion that the root CTest set contains no Bridge- or Message-Store-owned behavior suite names.

## 6. Documentation and validation

- [x] 6.1 Update root and actor test documentation with ownership rules, package-specific test options, tier commands, WebSocket determinism rules, and the current directory layout; remove stale `compose/` references and local ignored bytecode residue.
- [x] 6.2 Run the deterministic WebSocket suite repeatedly and verify correctness assertions contain no fixed sleeps or elapsed-time windows other than bounded deadlock watchdogs.
- [x] 6.3 Run root fast/full tests and verify all existing core scheduler, Asio, reload, reflection, packaging, CLI, and installed-SDK coverage remains registered and passing.
- [x] 6.4 Run Bridge and Message Store standalone suites, then the clean installed-SDK cross-repository conformance gate, and confirm actor behavior executes exactly once per conformance run.
- [x] 6.5 Run `nix fmt`, root and actor `git diff --check`, CTest inventory checks, and strict OpenSpec validation.
