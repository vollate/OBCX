## MODIFIED Requirements

### Requirement: Cross-repository actor conformance gates release
The OBCX release SHALL verify the checked-out Bridge, Message Store, actor-template, and actor-registry repositories against a clean installed SDK. Every standalone actor repository SHALL own and pass its actor-only build, behavior tests, installation checks, and smoke flows when configured as a top-level project. Root conformance SHALL coordinate and report those actor-owned suites, but MUST NOT duplicate their business test sources or automatically register them through embedded actor subdirectories in the root CTest inventory.

#### Scenario: All actor repositories are ready
- **WHEN** core full tests pass and Bridge, Message Store, actor-template, and registry pass the cross-repository conformance tier against the installed SDK
- **THEN** the coordinated actor-only release is eligible to proceed

#### Scenario: One repository fails conformance
- **WHEN** any required repository fails to build, run its owned tests, load, install, publish metadata, or complete its smoke flow
- **THEN** the coordinated release remains ineligible and diagnostics identify the owning repository and failing gate

#### Scenario: Actors are embedded in the root build
- **WHEN** root testing is enabled while production actor packages are added as subdirectories
- **THEN** their production artifacts are built but their repository-owned behavior tests are not added to the root CTest inventory

#### Scenario: Conformance configures an actor standalone
- **WHEN** the conformance tier configures Bridge or Message Store as a top-level consumer of the installed SDK
- **THEN** that actor's own complete test suite is enabled and executed exactly once for that conformance run
