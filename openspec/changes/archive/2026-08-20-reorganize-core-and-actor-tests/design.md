## Context

The root build loads Bridge and Message Store as actor packages through `add_subdirectory`. Because the actor repositories currently key test registration directly from the global `BUILD_TESTING` value, enabling root tests also registers 85 actor-owned cases in the root CTest inventory. Root runtime tests additionally include Bridge headers, compile Message Store source as reload fixtures, and carry installed pipeline/reload executables that assert actor behavior.

The two root WebSocket test files are the only direct coverage for concurrent `WebsocketClient` writes and OneBot action deadlines. They are excluded by default, rely on startup and response sleeps, and include a sequential `WeakNetworkWrites` case that does not force write-queue contention. `WebsocketClient::send` also polls a `std::future` every millisecond, making completion difficult to drive deterministically.

The change spans the root project, Bridge, Message Store, standalone installed-SDK conformance, and CI. Public bot/network APIs and actor ABI behavior must remain compatible, and tests must not expose access tokens, raw action responses, or other sensitive payloads.

## Goals / Non-Goals

**Goals:**

- Make repository ownership of every behavior test unambiguous.
- Keep root runtime tests independent of production actor source and headers.
- Run actor-owned suites once, from their own repositories, against the installed SDK.
- Replace sleep-driven WebSocket verification with controllable queue, write, response, deadline, and shutdown transitions.
- Put deterministic WebSocket reliability coverage in the normal CI gate.
- Preserve a clearly separated slow actor conformance tier.

**Non-Goals:**

- Reducing coverage by deleting core scheduler, Asio, reload, compile-contract, packaging, or actor behavior scenarios.
- Moving root-owned OneBot protocol/network tests into an actor repository.
- Changing the public `WebsocketClient`, `IConnectionManager`, bot, or actor ABI solely for testing.
- Replacing Boost.Beast or Boost.Asio, changing production timeout values, or introducing a general virtual-time framework.
- Making cross-repository conformance part of the fast local developer loop.

## Decisions

### 1. Actor packages register tests only when they own the build

Bridge and Message Store will use package-specific test options whose defaults follow `PROJECT_IS_TOP_LEVEL`. A standalone actor build with testing enabled registers its complete suite. When OBCX embeds the package to build its production DSO, actor tests remain disabled even though root testing is enabled.

This is preferred over temporarily mutating global `BUILD_TESTING`, which leaks across subdirectories and makes package behavior order-dependent. It also avoids root-maintained exclusion lists that must be updated whenever an actor adds a target.

### 2. Root tests use generic actor fixtures only

Root runtime generation, staging, dependency-isolation, drain, and reload tests will use minimal generic fixture actors under `tests/fixtures`. Same-SONAME and rebuilt-dependency cases will compile a generic lifecycle actor plus generation markers rather than Message Store implementation source.

The real Message Store → Bridge pipeline, forwarding behavior, mapping behavior, and bot-facing reload flow will move to Bridge's standalone suite; persistence behavior remains in Message Store's suite. The installed pipeline and reload smoke sources currently under root `tests/cpp` will therefore move to the owning actor suite or be replaced in root by actor-neutral SDK fixtures.

Generic fixtures remain valid in root because they exercise the V2 ABI and runtime lifecycle rather than an actor product's business contract.

### 3. Cross-repository conformance invokes actor-owned suites exactly once

The root conformance coordinator will install the SDK, configure each checked-out actor as a top-level project, build and install it, and invoke that repository's tests. Embedded actor tests will no longer have run earlier in the root CTest inventory, eliminating the current duplicate execution.

Root conformance may coordinate repositories and verify installed artifacts, but actor-specific test source, fixtures, expected events, schemas, and business assertions stay in the actor repository. The generic installed-SDK fixture remains in root to enforce the public SDK header and CMake surface independently of any production actor.

### 4. Use explicit fast, full, and conformance gates

CTest labels and presets will expose three operator-visible tiers:

- `fast`: deterministic root unit and bounded integration tests, including WebSocket reliability.
- `full`: fast plus reflection compile contracts, Python package/architecture checks, CLI validation, and installed-SDK smoke.
- `conformance`: full plus clean standalone builds, actor-owned tests, installation, pipeline, reload, and registry checks.

Normal CI runs full on every supported platform and conformance in the existing cross-repository job or stage. Experimental real-time network diagnostics, if retained, do not substitute for deterministic WebSocket tests.

### 5. Replace future polling with awaitable write completion

The WebSocket write queue will retain a bounded FIFO admission channel, but each admitted request will carry an Asio-native completion signal rather than a `std::promise` polled by a one-millisecond timer. `send` will await that signal. The writer completes each request exactly once with success, write failure, channel closure, or shutdown.

The queue logic will be factored behind a non-installed internal write operation seam. Production supplies the Beast `async_write`; tests supply a manually released operation that records starts, detects overlap, and controls each completion. This preserves the public `WebsocketClient` API while allowing deterministic contention and backpressure tests.

A queue failure or shutdown must retire every admitted waiter; no caller may remain suspended after the writer stops.

### 6. Model action completion as one terminal state transition

Pending OneBot actions will transition atomically from pending to one of response, timeout, transport failure, or cancellation. Response delivery and deadline expiry will compete through the same terminal operation, which removes the entry, cancels/disarms the losing source, and completes the awaiting caller once.

Deadline construction will use a non-installed internal factory. Production creates an Asio steady deadline; tests receive a manual deadline handle that can be fired explicitly. Tests can therefore force response-before-timeout, timeout-before-response, and close races without waiting for real seconds.

The production timeout and echo-correlation APIs remain unchanged. Diagnostics continue to identify only bounded echo/state information and never complete action payloads or credentials.

### 7. Wall-clock time is only a watchdog

Deterministic tests will wait on explicit barriers, channels, promises, or manual deadline events. A short real-time upper bound may fail a deadlocked test, but elapsed time or `sleep_for` ordering cannot be the assertion that proves queueing, timeout, or race behavior.

At least one bounded loopback smoke may remain to verify Beast handshake integration, provided server readiness and connection establishment use completion events rather than startup sleeps.

## Risks / Trade-offs

- **[Risk] Moving actor scenarios can accidentally weaken release coverage** → Inventory each moved case and require an actor-repository destination or an explicit generic root replacement before removing the root source.
- **[Risk] Package-specific test options behave differently for FetchContent consumers** → Default from `PROJECT_IS_TOP_LEVEL`, document an explicit override, and verify both embedded and standalone configurations.
- **[Risk] Internal test seams leak into the installed SDK** → Keep queue/deadline abstractions in non-installed detail headers and audit the installed header allowlist.
- **[Risk] Queue semantics change while removing future polling** → Preserve bounded admission and FIFO ordering, add exactly-once/failure/shutdown tests before switching implementation, and retain one loopback smoke.
- **[Risk] Manual deadlines diverge from Asio timer cancellation semantics** → Share one terminal pending-action state machine between production and fake deadlines; only the event source differs.
- **[Trade-off] Conformance remains slow because it performs clean external builds** → Keep it outside the fast tier and cache toolchains/build inputs rather than duplicating actor behavior in root tests.
- **[Trade-off] Actor failures may appear under a separate conformance stage** → Preserve repository/test names in CTest and CI output so ownership is clearer than the current combined inventory.

## Migration Plan

1. Add deterministic WebSocket queue and pending-action primitives and tests while retaining the existing loopback tests as temporary comparison coverage.
2. Remove polling completion and switch production WebSocket code to the tested primitives; enable deterministic tests in the full CI gate.
3. Add package-owned test options to Bridge and Message Store and verify standalone-on/embedded-off registration.
4. Replace root production-actor fixtures with generic actors and move every actor behavior/smoke case to its owner.
5. Update cross-repository conformance to run the actor-owned suites once against the installed SDK.
6. Remove superseded sleep-driven WebSocket cases, root actor-specific sources, stale cache artifacts, and obsolete test-layout documentation.
7. Run fast, full, conformance, formatting, diff, and strict OpenSpec validation gates.

Rollback can independently restore the previous WebSocket implementation or test registration while actor tests remain available in their standalone repositories. No data or deployment migration is involved.

## Open Questions

- Whether the remaining bounded Beast loopback smoke belongs in the normal `full` tier or a separate network-smoke label should be decided from its post-rewrite stability and runtime; deterministic queue and timeout coverage remains mandatory in normal CI either way.
