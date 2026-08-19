## Why

The root test tree currently mixes core/runtime verification with Bridge and Message Store behavior, while timing-sensitive WebSocket tests are disabled in normal CI and depend on wall-clock sleeps. This makes ownership unclear, repeats actor behavior across builds, and leaves important network concurrency and timeout behavior outside the default gate.

## What Changes

- Restrict root-owned tests to OBCX core/runtime, root network and protocol components, installed-SDK contracts, and generic actor fixtures.
- Move Bridge and Message Store behavior, repository, retry, formatting, and media coverage into their owning standalone actor repositories.
- Stop embedded actor packages from automatically registering their own tests in the root CTest inventory; exercise actor-owned suites through standalone installed-SDK conformance instead.
- Replace production-actor dependencies in core reload/generation tests with minimal generic fixtures that test only ABI, staging, lifecycle, and dependency-isolation behavior.
- Rewrite WebSocket queue and action-timeout coverage around controllable synchronization and timer hooks, removing fixed sleeps as correctness conditions.
- Replace the low-signal sequential `WeakNetworkWrites` scenario with deterministic concurrent backpressure, ordering, completion, timeout, boundary-race, and cleanup cases.
- Enable deterministic WebSocket reliability tests in normal CI while retaining a separate slow cross-repository conformance tier.
- Remove generated test artifacts and correct stale test-layout documentation.

## Capabilities

### New Capabilities
- `test-suite-ownership`: Defines root-versus-actor test ownership, generic fixture boundaries, and fast/full/conformance execution tiers.
- `websocket-runtime-reliability`: Defines serialized WebSocket write and correlated action-timeout behavior together with deterministic verification requirements.

### Modified Capabilities
- `actor-package-ecosystem`: Requires each standalone actor repository to own and run its behavior suite against the installed SDK while root conformance avoids duplicating actor business tests.

## Impact

- Root test registration under `tests/`, actor package loading, CTest labels/presets, and CI workflow selection.
- `WebsocketClient` and OneBot `WebSocketConnectionManager` test seams for controlled backpressure, completion, and timeout advancement.
- Bridge and Message Store test CMake ownership and standalone conformance invocation.
- Generic actor fixtures used by runtime generation, package staging, pipeline, and reload tests.
- Test documentation and ignored generated Python cache files.
