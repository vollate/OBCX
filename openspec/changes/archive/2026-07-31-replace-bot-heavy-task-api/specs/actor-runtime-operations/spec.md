## MODIFIED Requirements

### Requirement: Runtime owns one thread-budget policy
The runtime SHALL size actor workers, Asio I/O executors, and blocking/CPU
executors from one explicit thread-budget policy. An automatic value MUST avoid
independently allocating `hardware_concurrency()` threads to every pool. The
resolved `blocking_workers` allocation MUST back exactly one process-owned
`BlockingExecutor` that is registered as a service in the startup generation
and shared unchanged with every reload candidate. A change to the resolved
blocking allocation MUST remain restart-required, and validation-only startup
MUST validate the allocation without creating blocking worker threads.

#### Scenario: Automatic worker sizing
- **WHEN** worker counts are configured as automatic
- **THEN** startup resolves and logs a bounded allocation for actor, I/O, and blocking workers from one runtime budget

#### Scenario: Invalid explicit sizing
- **WHEN** configured worker counts are zero where zero is not automatic or exceed supported runtime limits
- **THEN** startup fails validation with an actionable configuration error

#### Scenario: Startup registers the blocking service
- **WHEN** normal startup resolves a valid runtime thread budget
- **THEN** it creates one blocking executor with the resolved blocking worker count and makes that same service available to actor contexts before ingress

#### Scenario: Reload reuses the process blocking service
- **WHEN** a reload candidate preserves the active process-owned thread fingerprint
- **THEN** the candidate receives the exact active process blocking executor rather than constructing generation-local workers

#### Scenario: Reload changes blocking worker count
- **WHEN** a reload candidate resolves a different blocking worker count
- **THEN** preparation fails with `reload_restart_required` before cutover and leaves the active executor unchanged

#### Scenario: Validation-only startup has no blocking threads
- **WHEN** `--validate-config` validates a configuration with a valid blocking worker allocation
- **THEN** validation succeeds without constructing, starting, or stopping a blocking executor
