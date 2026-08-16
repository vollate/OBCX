## MODIFIED Requirements

### Requirement: Runtime extension lifecycle is actor-only
The runtime SHALL discover, construct, dispatch, reload, and shut down
extensions only through the supported V2 actor contract and actor pipelines.
Production and SDK artifacts SHALL contain no `IPlugin`, `PluginManager`,
plugin lifecycle callback, plugin export symbol, plugin reload command, or
plugin loader branch. Operator lifecycle entry points, including `reload`, MUST
operate exclusively through the V2 actor runtime and MUST NOT provide plugin
compatibility behavior.

#### Scenario: Runtime starts configured extensions
- **WHEN** OBCX starts with valid actor and pipeline configuration
- **THEN** it loads V2 actors without constructing or consulting a plugin manager

#### Scenario: Retired runtime surfaces are audited
- **WHEN** production sources, binaries, public headers, and CLI commands are inspected
- **THEN** no plugin lifecycle or plugin reload entry point is present and any actor-runtime reload entry point operates exclusively through V2 actor generations
