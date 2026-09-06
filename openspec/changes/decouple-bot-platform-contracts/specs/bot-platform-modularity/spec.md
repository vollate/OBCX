## ADDED Requirements

### Requirement: Public bot contracts have one-way platform dependencies
The common Bot SDK SHALL contain only platform-independent identifiers, references, errors, results, gateway/typed invocation machinery, and the existing common send/delete contracts. Telegram and OneBot SHALL each own their action declarations, provider-specific request/result types, validation, JSON codecs, and typed client adapters. Common SDK headers MUST NOT include either platform contract, enumerate their operations, or declare Telegram topic/entity/media values. A platform SDK MUST NOT depend on the other platform SDK or on process transport/component headers.

#### Scenario: Common SDK is consumed alone
- **WHEN** a fixture is compiled with only installed common SDK headers and their non-platform dependencies
- **THEN** identifiers, common sends, results, and fake-gateway invocation compile without any OneBot or Telegram headers

#### Scenario: OneBot actor is compiled without Telegram
- **WHEN** a standalone actor includes only the common and OneBot SDK targets and the install prefix contains no Telegram SDK
- **THEN** its OneBot request/result codecs and typed calls compile and run against a fake gateway

#### Scenario: Telegram actor is compiled without OneBot
- **WHEN** a standalone actor includes only the common and Telegram SDK targets and the install prefix contains no OneBot SDK
- **THEN** its topic, edit, and media request/result codecs and typed calls compile and run against a fake gateway

### Requirement: Surface and action identities are extensible value data
The common SDK SHALL represent surfaces and actions as explicitly constructed, owning `SurfaceId` and `ActionId` values with deterministic exact string equality and JSON conversion. Stable IDs MUST be nonempty, at most 128 bytes, and contain only lowercase ASCII letters, digits, dots, underscores, or hyphens. Syntax validation MUST NOT enumerate supported platforms or actions, infer aliases, or install a default identity. Production availability SHALL be determined separately by registered modules, recipes, and endpoint actions. The common SDK MUST NOT retain `BotSurface`, `BotAction`, a global action list, or a platform/action switch matrix.

#### Scenario: A syntactically valid unregistered surface is decoded
- **WHEN** a generic identity value contains `test.echo`
- **THEN** syntax decoding succeeds without adding a core enum, but production configuration and routing reject it unless the exact test module is explicitly registered in that isolated runtime

#### Scenario: Identity is malformed
- **WHEN** an ID is empty, oversized, contains uppercase or control characters, or contains path separators
- **THEN** construction/deserialization rejects it without normalizing it to another identity

### Requirement: Platform registration is explicit and process owned
A process-owned immutable platform catalog SHALL be supplied to configuration, recipe assembly, and generation validation. Each platform module SHALL own its connection parser, typed configuration plan, recipes, operation descriptors/handlers, ingress metadata, and command adapter binding. Only the application composition root SHALL enumerate the built-in platform modules. Generic runtime MUST NOT include platform contracts, inspect platform config variants, or perform platform-specific dispatch switches. Registration MUST reject duplicate recipe keys and MUST be sealed before configuration preparation; actors MUST NOT receive the platform catalog or register process handlers.

#### Scenario: Built-in application starts
- **WHEN** the composition root registers the current OneBot and Telegram modules
- **THEN** configuration can select only the existing OneBot WebSocket, OneBot HTTP, and Telegram HTTP recipes and no module is discovered through static initialization or user-supplied component classes

#### Scenario: Test module extends an isolated runtime
- **WHEN** a separate translation unit explicitly registers a fake `test.echo` platform, parser, recipe, and operation in a test-only catalog
- **THEN** generic parse, describe, assemble, dispatch, and shutdown succeed without editing common SDK/runtime or linking either production platform

#### Scenario: Recipe key is registered twice
- **WHEN** two modules register the same exact surface/transport key
- **THEN** catalog construction fails before reading credentials or constructing a transport

### Requirement: Production manifests remain reviewed and executable
The application SHALL continue to ship only `onebot11.qq` and `telegram.bot_api`, the existing three recipes, and the existing union of 13 action IDs. Platform operation descriptors SHALL be the shared source for static validation manifests and executable endpoint registration. Supported-action publication MUST describe installed executable operations, not speculative platform methods. Extensibility fixtures MUST remain isolated from production registration and current actor business suites.

#### Scenario: Production manifest conformance runs
- **WHEN** tests collect descriptors from the two built-in modules
- **THEN** the surface/recipe/action sets match the recorded production baseline and each advertised operation has a registered codec, validator, handler, and executable success test

#### Scenario: A platform operation is extended in a test module
- **WHEN** a test-only module adds another typed operation descriptor
- **THEN** its handler becomes available through the same gateway without a new central enum entry or virtual overload, while production manifests remain unchanged

### Requirement: SDK and runtime isolation are verified by builds
CMake SHALL export independently consumable common, OneBot, and Telegram SDK targets with no common-to-platform or platform-to-other-platform public dependency. Automated checks SHALL compile the common and single-platform include closures in fresh install prefixes and compile the generic runtime without production platform implementations. A file move or text scan alone MUST NOT constitute modularity acceptance.

#### Scenario: An accidental umbrella include is introduced
- **WHEN** a common or OneBot-only SDK header transitively includes a Telegram contract
- **THEN** the isolated build fails even if the combined production build succeeds

#### Scenario: Fresh install is inspected
- **WHEN** SDK installation is performed into an empty prefix
- **THEN** only explicitly exported SDK headers appear and no process catalog, typed credential config, transport, or component registry is exported through the new Bot SDK targets
