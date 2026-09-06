## ADDED Requirements

### Requirement: Gateway media bytes use bounded binary values
In-process gateway codecs SHALL represent media byte fields using owning `Json::binary` values rather than arrays of JSON numbers. Public DTO JSON codecs and recorded golden fixtures SHALL remain unchanged. Gateway codecs MUST transfer byte-buffer ownership directly, validate request/SDK bounds before copying or delivering byte values, and MUST NOT create intermediate numeric byte arrays or dump/parse strings. The gateway format SHALL NOT be a durable or network protocol and MUST NOT add a blob service, unbounded cache, provider passthrough, or configuration defaults.

#### Scenario: Typed upload crosses the gateway
- **WHEN** a Telegram typed adapter encodes a valid multipart upload
- **THEN** each gateway media byte field is binary and retains its exact bytes without per-byte JSON object allocation

#### Scenario: Gateway file result is decoded
- **WHEN** the typed adapter receives a valid bounded binary file result
- **THEN** it transfers the buffer to the SDK result, preserves metadata and checks the original request bound

#### Scenario: Raw caller supplies numeric media bytes
- **WHEN** a caller bypasses the typed adapter and supplies an array or another non-binary value in a gateway media byte field
- **THEN** endpoint decoding rejects the payload before provider I/O without implicitly converting an unbounded array

#### Scenario: Public JSON fixtures are replayed
- **WHEN** public DTO JSON upload/fetch codecs round-trip their recorded numeric byte arrays
- **THEN** their wire semantics remain unchanged independently of the internal gateway binary representation

## MODIFIED Requirements

### Requirement: The process registers exact QQ and Telegram operation wrappers
The process SHALL register one platform-independent operation endpoint for each enabled composed installation using its exact configured installation id and module-registered surface. Registration MUST reject duplicate installation ids, unregistered surfaces, and inconsistent endpoint identity. Production registration SHALL use only OneBot and Telegram modules. Endpoint APIs MUST NOT accept live universal/provider bots or enumerate provider request types; each endpoint SHALL expose a sealed action registry installed by its owning module.

#### Scenario: Composed installations publish endpoints
- **WHEN** enabled OneBot and Telegram installations complete preparation
- **THEN** their endpoints are addressable by exact installation ids and surfaces without wrapping or casting a live bot

#### Scenario: Duplicate id is registered
- **WHEN** two endpoints attempt to use the same installation id
- **THEN** startup rejects the duplicate before operation dispatch begins

#### Scenario: Official QQ is requested
- **WHEN** production registration is attempted with `qq.official`
- **THEN** registration fails rather than exposing a OneBot endpoint under that surface

#### Scenario: Legacy live bot is supplied
- **WHEN** production operation registration APIs are inspected
- **THEN** none accepts `IBot`, provider bot interfaces, concrete bots, or live-bot registry entries

### Requirement: Supported actions match the closed implementation matrix
Each endpoint SHALL publish a deterministic data-only set of `ActionId` values derived from its installed executable descriptors. A descriptor MUST bind request/result codecs, scope validation, submission classification, and a handler; publication MUST NOT accept a bare string without an executable binding. Production sets SHALL match the platform-owned descriptors for the current action matrix. Optional Telegram multipart upload MUST be advertised only when that installation has a prepared uploader. Capability inference from class inheritance, prefixes, unrelated installations, or a global enum MUST NOT occur.

#### Scenario: Telegram endpoint is inspected
- **WHEN** a caller queries a prepared Telegram installation
- **THEN** it sees only that installation's implemented common and Telegram action IDs

#### Scenario: OneBot endpoint is inspected
- **WHEN** a caller queries a prepared OneBot installation
- **THEN** it sees common send/delete and current OneBot action IDs, never Telegram or official-QQ actions

#### Scenario: Multipart uploader capability is absent
- **WHEN** a Telegram test recipe omits its uploader
- **THEN** upload is absent from the support set and an attempted upload performs no provider I/O

#### Scenario: Action is registered twice
- **WHEN** two handlers claim the same action in one endpoint
- **THEN** preparation rejects the duplicate instead of selecting a handler by insertion order

### Requirement: Actors call one data-only operation client
The runtime SHALL expose one `BotOperationGateway` actor service accepting a platform-independent envelope of exact installation, `ActionId`, and SDK request JSON values with binary media fields, and returning SDK result JSON values with binary media fields or a validated common error. Common and platform SDK typed adapters SHALL pair each request with its result type and perform encoding/decoding around the gateway. The gateway and generic endpoint MUST NOT declare per-platform virtual overloads or include all platform contracts. Actors MUST NOT receive process registries, handlers, transports, credentials, provider executors, or raw provider response envelopes.

#### Scenario: Actor sends to a configured group
- **WHEN** an actor invokes the common typed group-send adapter with exact installation and group target
- **THEN** the gateway selects the matching endpoint and the actor receives a validated typed result without resolving a bot

#### Scenario: Request supplies only a platform
- **WHEN** a caller omits the installation id or supplies only `qq`, `telegram`, or a surface
- **THEN** the gateway returns a definitely-not-submitted validation error and selects no bot

#### Scenario: Target belongs to another installation
- **WHEN** envelope identity differs from a payload target, message, or reply installation
- **THEN** endpoint adapter validation rejects it before the provider handler runs

#### Scenario: Caller bypasses the typed adapter
- **WHEN** a raw gateway envelope names an unknown action, conflicts with its payload action, or contains an invalid typed payload
- **THEN** the gateway/endpoint returns a definitely-not-submitted error and never forwards an arbitrary provider method or endpoint URL

#### Scenario: Typed result decoding fails
- **WHEN** a completed operation produces invalid SDK success data at the gateway boundary
- **THEN** the typed adapter returns a validated error rather than leaking a decoder exception or inventing success, and a side effect remains possibly submitted unless non-submission is proven

### Requirement: Wrappers parse current provider responses conservatively
Owning platform operation modules SHALL parse current Telegram `{ok,result}` and error envelopes and OneBot `status/retcode/data/echo` envelopes. Successful side effects MUST satisfy existing expected result shape, count, scoped message identity, and mutation-confirmation checks. Empty, malformed, or synthetic provider responses MUST NOT be reported as success. Platform modules SHALL preserve redacted provider errors and conservative submission safety through gateway encoding; generic dispatch MUST NOT parse provider envelopes.

#### Scenario: Telegram group or topic send succeeds
- **WHEN** the Telegram module receives a valid single-message result
- **THEN** its adapter returns the correct typed chat/message reference

#### Scenario: OneBot group send succeeds
- **WHEN** the OneBot module receives successful data with `message_id`
- **THEN** its adapter returns the requested group's scoped reference

#### Scenario: Provider response is malformed
- **WHEN** a side-effecting call receives invalid JSON, missing identity, or an incorrect Telegram media-group result shape/count
- **THEN** the module returns a possibly-submitted typed error and no fabricated success

#### Scenario: Provider explicitly rejects the action
- **WHEN** a valid provider error envelope is returned
- **THEN** the module preserves only redacted code/message/retry metadata and the existing submission-safety classification through the gateway

### Requirement: Bot-owned media and lookup calls stay behind wrappers
Telegram authenticated file resolution/download and media sends SHALL execute inside Telegram module handlers. Current OneBot member, forwarded-message, file-resolution, and poke calls SHALL execute inside OneBot module handlers. Handlers and SDK codecs MUST preserve existing request/configured byte bounds. Gateway results MUST contain SDK values only, not tokenized Telegram URLs, provider clients, transport objects, arbitrary local paths, or component registries.

#### Scenario: Telegram media is fetched
- **WHEN** Bridge requests a Telegram file within its byte bound
- **THEN** the Telegram module resolves and downloads it and the typed adapter returns bounded bytes and metadata

#### Scenario: Telegram file exceeds its bound
- **WHEN** declared or downloaded media exceeds the current request/configured maximum
- **THEN** the module returns the existing classified size failure and discards the oversized value without an unbounded gateway cache

#### Scenario: OneBot provider-specific lookup runs
- **WHEN** Bridge invokes a registered OneBot member, forward, or file operation
- **THEN** the owning module validates the provider response and returns its platform-specific typed value

### Requirement: Existing process lifecycle and compatibility remain authoritative
One shared process gateway SHALL route to composed installation endpoints across actor generations. Catalogs and endpoint registrations MUST remain process-owned and sealed for normal operation. Actor reload MUST NOT recreate installations, providers, or gateway threads. Validation-only SHALL inspect module plans and manifests without constructing transports, probing providers, publishing catalogs, or creating operation persistence. Existing admission, cancellation, and generation lease rules SHALL retain gateway envelopes, handlers, capabilities, and actor continuations until completion or bounded cancellation. Removed live-bot compatibility APIs MUST NOT return.

#### Scenario: Actor generation reloads
- **WHEN** a candidate actor generation is built
- **THEN** it receives the same gateway and validates exact routes without recreating installations

#### Scenario: Validation-only startup runs
- **WHEN** the configuration is validated without normal startup
- **THEN** module metadata and operation manifests are checked without transport construction or provider/state side effects

#### Scenario: Process shuts down
- **WHEN** shutdown begins while gateway invocation awaits provider I/O
- **THEN** admission closes and required endpoint/module/executor and actor-generation state remain alive through completion or cancellation, with no detached completion after DSO retirement

#### Scenario: Legacy compatibility API is checked
- **WHEN** installed SDK and production architecture checks run
- **THEN** no live-bot registry, provider interface, old all-platform operation client, or concrete bot is exposed as an actor service

### Requirement: Dispatcher coverage is limited to testable QQ and Telegram paths
Production conformance SHALL cover every registered OneBot and Telegram action with mock transports, including exact routing, wrong surface, unsupported action, forged envelope/payload, valid success, explicit failure, malformed response, media bounds, and uncertain side effects. Generic runtime tests SHALL additionally use an isolated synthetic module to prove extensibility without a central switch; it MUST NOT be linked into production or current actor business tests. No test SHALL require production credentials or a new real-platform adapter.

#### Scenario: Advertised-action conformance runs
- **WHEN** the production endpoint suites enumerate supported actions
- **THEN** every advertised action has a success test and the combined set remains the current 13 IDs

#### Scenario: Dispatcher architecture is checked
- **WHEN** generic gateway and endpoint infrastructure compile without production platform SDKs
- **THEN** they route the isolated fake module using the same registration mechanism and contain no platform DTO dependency or live-bot cast
