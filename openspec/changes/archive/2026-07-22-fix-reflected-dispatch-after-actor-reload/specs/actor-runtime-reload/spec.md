## ADDED Requirements

### Requirement: Reload gate wakeups survive waiter registration races

Opening or aborting the reload ingress gate SHALL wake every published waiter
even when the wake operation executes immediately before that waiter's
asynchronous timer wait is registered. Waiting ingress MUST recheck the
authoritative gate and shutdown state after every wakeup.

#### Scenario: Gate opens before asynchronous wait registration

- **WHEN** ingress publishes its waiter, the reload gate opens, and the posted wake executes before ingress initiates the asynchronous wait
- **THEN** the subsequent wait completes immediately and the message proceeds against the generation selected by the reopened gate

#### Scenario: Gate opens after asynchronous wait registration

- **WHEN** ingress is already suspended on its waiter when the reload gate opens
- **THEN** the waiter is woken and the message proceeds exactly once after rechecking gate state

### Requirement: Repeated reload preserves observable bot ingress

Actor-runtime reload SHALL preserve real bot event ingress across repeated
staged actor generations. A message submitted before gate closure, while the
gate is closed, or after cutover MUST produce a terminal orchestrator result,
and a non-success result MUST be reported with structured routing and actor
failure details rather than discarded.

#### Scenario: Messages straddle a successful repeated reload

- **WHEN** bot messages arrive during candidate preparation, while the ingress gate is closed for drain, and after a later-generation cutover
- **THEN** all messages complete exactly once on the generation selected at their admission boundary and supported reflected inputs reach that generation's handlers

#### Scenario: Actor routing returns a failure

- **WHEN** bot ingress completes without throwing but its orchestrator result contains one or more actor failures
- **THEN** OBCX logs the platform, bot identity, failure count, first failing pipeline, stage, actor, stable code, retryability, and safe message without logging the message payload
