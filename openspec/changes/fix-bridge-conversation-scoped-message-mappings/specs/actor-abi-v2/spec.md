## ADDED Requirements

### Requirement: V2 actors may prepare a generation before route activation
The V2 export helper SHALL provide an additive generation-preparation entry point. Actors MAY implement a typed `prepare_generation(ActorContext&)` hook; actors without that hook and previously built V2 libraries without the additive symbol MUST remain loadable and SHALL be treated as ready. The runtime MUST invoke preparation after validated configuration and generation services are available but before registering the actor for scheduler, command, or pipeline ingress. Preparation MUST return one of ready, failed, or restart-required without exposing actor callables through the input contract.

#### Scenario: Actor prepares startup-owned state
- **WHEN** a startup generation constructs an actor with a generation-preparation hook
- **THEN** the runtime supplies its exact actor id, database scope, configuration view, generation purpose, and runtime services before any route can invoke that actor

#### Scenario: Preparation fails
- **WHEN** an actor returns a failed preparation result
- **THEN** generation construction fails with the bounded actor diagnostic and no route is published to that generation

#### Scenario: Preparation requires process restart
- **WHEN** a reload candidate reports that actor-owned state requires a startup-only migration
- **THEN** candidate construction returns typed `reload_restart_required`, leaves the active generation authoritative, and does not publish the candidate

#### Scenario: Existing V2 actor has no preparation export
- **WHEN** ActorManager loads a valid previously built V2 actor that lacks the additive preparation symbol
- **THEN** it preserves existing construction and dispatch behavior by treating preparation as ready

#### Scenario: Validation-only preparation runs
- **WHEN** validation-only generation construction invokes an actor preparation hook
- **THEN** the actor can validate actor-specific configuration from `ActorGenerationPurpose::ValidationOnly` without starting ingress or mutating actor-owned state
