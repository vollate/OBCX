## ADDED Requirements

### Requirement: Runtime uses one native actor engine
The actor-only runtime SHALL construct and use the native ActorTask scheduler
as its sole actor engine. Runtime configuration SHALL NOT expose an actor
engine selector, and the release SHALL contain no Asio-v1 scheduler or
in-process engine fallback.

#### Scenario: Runtime starts actor scheduling
- **WHEN** OBCX starts with valid actor and pipeline configuration
- **THEN** all actor invocations execute through the native scheduler without selecting an engine

#### Scenario: Runtime artifacts are inspected
- **WHEN** production binaries, installed libraries, and configuration schemas are inspected
- **THEN** no Asio-v1 engine implementation, selector, or rollback probe is present

### Requirement: Runtime extension lifecycle is actor-only
The runtime SHALL discover, construct, dispatch, and shut down extensions only
through the supported V2 actor contract and actor pipelines. Production and
SDK artifacts SHALL contain no `IPlugin`, `PluginManager`, plugin lifecycle
callback, plugin export symbol, plugin reload command, or plugin loader branch.

#### Scenario: Runtime starts configured extensions
- **WHEN** OBCX starts with valid actor and pipeline configuration
- **THEN** it loads V2 actors without constructing or consulting a plugin manager

#### Scenario: Retired runtime surfaces are audited
- **WHEN** production sources, binaries, public headers, and CLI commands are inspected
- **THEN** no plugin lifecycle or reload entry point is present

### Requirement: Retired extension inputs receive no special handling
Runtime and build configuration SHALL consume only the supported actor
configuration and canonical actor package metadata. They SHALL contain no
plugin-specific parser, accessor, dependency extraction, detector, translator,
migration warning, or automatic rewrite for `[plugins]`, `plugins.toml`, or
`plugin.toml`.

#### Scenario: Supported actor input is processed
- **WHEN** actor runtime configuration or canonical actor package metadata is supplied
- **THEN** the owning runtime or build tool processes it without consulting a plugin input

#### Scenario: Retired plugin input is present
- **WHEN** an otherwise unused TOML key or file resembles a retired plugin input
- **THEN** only generic parser behavior applies and no plugin-specific diagnostic or migration path runs

## MODIFIED Requirements

### Requirement: Rollout is gated by recorded baselines
Before the actor-only removal phase completes, native scheduling SHALL pass the
recorded correctness, balanced-load, skewed-load, I/O-heavy,
completion-storm, shutdown, CPU, and memory acceptance thresholds. The
balanced-load threshold previously missed by native-v2 MUST pass on the
designated reproducible benchmark environment. Once the actor-only release is
built, it SHALL contain no secondary engine for runtime rollback.

#### Scenario: Native runtime misses a cutover threshold
- **WHEN** any required correctness or performance threshold fails
- **THEN** the actor-only cutover remains incomplete and the removal phase is not eligible for release

#### Scenario: Native runtime passes every cutover threshold
- **WHEN** all recorded gates and standalone actor integration tests pass
- **THEN** the removal phase may complete and the native scheduler becomes the only shipped engine

#### Scenario: Released runtime needs operational rollback
- **WHEN** an operator must roll back after deploying the actor-only release
- **THEN** recovery uses deployment of the preceding OBCX release rather than an alternate engine in the actor-only binary

## REMOVED Requirements

### Requirement: Runtime engine is selectable
**Reason**: The compatibility window ends with this cutover, leaving one native
actor engine and no in-process fallback.

**Migration**: Remove engine selection from configuration. Operational rollback
deploys the preceding OBCX release instead of selecting `asio-v1`.
