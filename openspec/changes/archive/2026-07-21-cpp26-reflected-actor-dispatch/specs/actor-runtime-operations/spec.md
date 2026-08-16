## ADDED Requirements

### Requirement: Startup validates pipelines against loaded actor contracts
Runtime startup SHALL parse and syntactically validate configuration, load the
enabled actor libraries and input contracts, and perform actor-aware pipeline
validation before creating scheduler workers, services, bots, or ingress.
Actor-aware validation MUST reject a stage whose actor is unavailable or whose
configured input is absent from that actor's accepted-input contract.

#### Scenario: Stage input is supported
- **WHEN** every configured stage references a loadable enabled actor and one of its accepted canonical inputs
- **THEN** actor-aware validation succeeds and runtime activity may be created

#### Scenario: Actor does not accept stage input
- **WHEN** a stage configures a canonical input that is absent from its actor's input contract
- **THEN** startup fails before scheduler workers or ingress start and identifies the pipeline, stage, actor, and input

#### Scenario: Actor library cannot supply a contract
- **WHEN** an enabled actor library fails ABI or input-contract loading
- **THEN** startup fails before runtime activity starts and no actor is partially registered

### Requirement: Validation-only CLI has no runtime side effects
The CLI SHALL provide `obcx --validate-config <config>` and SHALL run the same
configuration parsing, actor-library loading, input-contract validation, and
pipeline validation used by normal startup. It MUST exit after validation
without constructing actor workers, starting services or bots, or opening
ingress.

#### Scenario: Configuration is valid
- **WHEN** validation-only mode receives a valid deployable configuration and actor library set
- **THEN** it exits successfully after unloading validation resources without starting runtime activity

#### Scenario: Configuration is invalid
- **WHEN** validation-only mode encounters an actor contract or pipeline validation error
- **THEN** it exits unsuccessfully with the same actionable diagnostic normal startup would produce

### Requirement: Pipeline validation is local and deterministic
Pipeline validation SHALL check unique stage names, existence of `after`
references, acyclicity of the explicit `after` dependency graph, actor
availability and accepted inputs, and existing scheduler/database/service
constraints. It SHALL NOT infer actor output sets, input/output pairs, global
message reachability, business-branch feasibility, or a static message-flow
graph from pipeline `output` declarations.

#### Scenario: Explicit dependency graph contains a cycle
- **WHEN** configured `after` edges form a directed cycle
- **THEN** validation rejects the pipeline with the participating stage path

#### Scenario: Output declaration cannot prove reachability
- **WHEN** stage inputs and explicit dependencies are locally valid but an `output` declaration does not prove a realizable business path
- **THEN** local validation does not claim or attempt a global dataflow proof

#### Scenario: Stage name or dependency is invalid
- **WHEN** a pipeline repeats a stage name or references a missing `after` stage
- **THEN** validation rejects the pipeline before runtime activity starts

### Requirement: Executed routes carry branch-local cycle context
Each executed message route SHALL carry an internal hop count and ancestor
sequence of `(pipeline, stage, message_type)`. Entering an ancestor node already
present in the same route MUST terminate that branch with
`message_routing_cycle`. Fan-out branches MUST receive independent trace state,
and terminal asynchronous routing MUST retain its trace across suspension.

#### Scenario: Actual feedback route repeats an ancestor
- **WHEN** emitted messages route back to the same pipeline, stage, and canonical message type within one branch
- **THEN** that branch terminates with `message_routing_cycle` and a bounded trace of the repeated route

#### Scenario: Sibling branch visits the same node
- **WHEN** two fan-out siblings independently visit the same route node without either repeating its own ancestor
- **THEN** neither sibling is rejected because of the other sibling's trace

#### Scenario: Terminal asynchronous route resumes
- **WHEN** a terminal asynchronous actor result suspends and later emits a message
- **THEN** descendant routing continues with the originating branch's hop and ancestor context

### Requirement: Executed routes fail explicitly at the hop limit
The runtime SHALL enforce a bounded routing hop limit with a default of 32.
Exceeding the effective limit MUST terminate the branch with
`message_routing_hop_limit`; the orchestrator MUST NOT silently return or drop
the message because of routing depth.

#### Scenario: Non-repeating route exceeds the limit
- **WHEN** an executed route traverses more nodes than its effective hop limit without repeating an ancestor tuple
- **THEN** the branch terminates with `message_routing_hop_limit` and identifies the route context

#### Scenario: Route reaches the last permitted hop
- **WHEN** a route completes at or below its effective hop limit
- **THEN** the runtime preserves normal result and failure behavior

### Requirement: Dispatch and routing diagnostics protect message contents
Dispatch and routing failures SHALL use stable codes including
`unsupported_message_type`, `invalid_message_payload`,
`message_routing_cycle`, and `message_routing_hop_limit`. Diagnostics SHALL
include available actor, pipeline, stage, canonical type, message or correlation
identity, and bounded route information, and MUST NOT include complete message
payloads by default.

#### Scenario: Routing cycle is reported
- **WHEN** an executed route terminates because it repeats an ancestor node
- **THEN** logs and failure state use `message_routing_cycle` with bounded routing identity and no complete payload

#### Scenario: Invalid payload is reported
- **WHEN** actor dispatch cannot deserialize a supported message payload
- **THEN** logs and failure state use `invalid_message_payload` with actor and message identity but no complete payload
