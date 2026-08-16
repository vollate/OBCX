## MODIFIED Requirements

### Requirement: Explicit actor ABI generation
Every actor library SHALL expose the supported numeric V2 ABI generation,
independent from the actor's semantic version. ActorManager MUST determine the
generation before interpreting factory results or dispatching messages and
SHALL implement no loader path for earlier actor or plugin ABIs.

#### Scenario: Supported V2 generation loads
- **WHEN** an actor library reports the supported V2 ABI generation and provides all required V2 symbols
- **THEN** ActorManager constructs and registers it as an IActorV2

#### Scenario: Library does not provide the V2 contract
- **WHEN** a dynamic library lacks the supported V2 generation or required V2 symbols
- **THEN** ActorManager does not construct or register an actor from that library

## REMOVED Requirements

### Requirement: V1 and V2 actors coexist
**Reason**: The compatibility window is complete and the actor-only runtime has
one supported ABI.

**Migration**: No runtime migration path is provided; rebuild the component as
an `IActorV2` actor before deploying the actor-only release.

### Requirement: V1 Asio actors adapt to native scheduling
**Reason**: V1 actor dispatch and its Asio adapter are removed; V2 actors use
the explicit actor-to-Asio interoperability boundary.

**Migration**: No adapter remains; implement `IActorV2` and use
`ActorContext::await_asio` for required I/O.

### Requirement: V1 removal requires a later compatibility decision
**Reason**: This change is the explicit later decision that ends the V1 and
Asio-v1 compatibility window after all cutover gates pass.

**Migration**: Deploy only V2 actor binaries with the actor-only release.
