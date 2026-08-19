## ADDED Requirements

### Requirement: The process registers exact QQ and Telegram operation wrappers
The process SHALL register one operation wrapper for each enabled existing `QQBot` or `TGBot` using its configured bot name as installation id and `onebot11.qq` or `telegram.bot_api` as its exact surface. Registration MUST reject duplicate installation ids and MUST NOT register any other surface in this change.

#### Scenario: Existing bots are wrapped
- **WHEN** one enabled QQ bot and one enabled Telegram bot complete normal process setup
- **THEN** both are addressable by their configured names through their exact wrappers

#### Scenario: Duplicate id is registered
- **WHEN** two wrappers attempt to use the same installation id
- **THEN** startup rejects the duplicate before actor operation dispatch begins

#### Scenario: Official QQ is requested
- **WHEN** registration is attempted with `qq.official`
- **THEN** registration fails rather than wrapping the OneBot bot under that surface

### Requirement: Supported actions match the closed implementation matrix
Each wrapper SHALL publish a data-only supported-action set containing only operations implemented by that concrete existing bot and listed in `qq-telegram-bot-contract`. Optional Telegram multipart media-group upload MUST be advertised only when the wrapped bot provides its current uploader interface. Static support reporting MUST NOT infer history, contacts, moderation, private send, or any unsupported-platform operation from `IBot` method presence.

#### Scenario: Telegram wrapper is inspected
- **WHEN** a caller queries a current Telegram installation
- **THEN** it sees only the listed common and `telegram.*` actions implemented by `TGBot`

#### Scenario: OneBot wrapper is inspected
- **WHEN** a caller queries a current QQ installation
- **THEN** it sees only common send/delete and the listed `onebot11.*` actions and never sees Telegram or official-QQ actions

#### Scenario: Multipart uploader is absent
- **WHEN** a Telegram test double does not implement the current media-group uploader interface
- **THEN** `telegram.media.send_group_uploads` is absent and an attempted call fails before provider I/O

### Requirement: Actors call one data-only operation client
The runtime SHALL expose `BotOperationClient` as an actor service that accepts only the finite request values and returns typed results. The client SHALL delegate to the process wrapper and MUST NOT expose `BotRegistry`, `IBot`, provider interfaces, concrete bots, connection managers, credentials, or provider executors through its public API.

#### Scenario: Actor sends to a configured group
- **WHEN** an actor submits a valid request with exact installation and group target
- **THEN** the matching process wrapper performs the call and the actor receives a typed result without resolving a bot

#### Scenario: Request supplies only a platform
- **WHEN** a caller omits the installation id or supplies only `qq`, `telegram`, or a surface
- **THEN** dispatch returns a definitely-not-submitted validation error and selects no bot

#### Scenario: Target belongs to another installation
- **WHEN** a request combines one installation with a group or message reference scoped to another
- **THEN** dispatch rejects it before invoking the wrapper

### Requirement: Wrappers parse current provider responses conservatively
The Telegram wrapper SHALL parse current `{ok,result}` success and Telegram error envelopes. The OneBot wrapper SHALL parse current `status/retcode/data/echo` envelopes. Successful side-effect results MUST contain required scoped message ids or typed mutation status; empty, malformed, and synthetic legacy responses MUST NOT be reported as success. Side-effect exceptions after invocation begins SHALL use conservative submission safety.

#### Scenario: Telegram group or topic send succeeds
- **WHEN** the existing Telegram bot returns a valid result message
- **THEN** the wrapper returns its scoped chat/message reference

#### Scenario: OneBot group send succeeds
- **WHEN** the existing QQ bot returns successful data with `message_id`
- **THEN** the wrapper returns its scoped group/message reference

#### Scenario: Provider response is malformed
- **WHEN** a side-effecting provider call returns an empty body, invalid JSON, or nominal success without required fields
- **THEN** the wrapper returns a typed possibly-submitted error and does not fabricate success

#### Scenario: Provider explicitly rejects the action
- **WHEN** a valid provider error envelope is returned
- **THEN** the wrapper preserves only redacted code/message/retry metadata needed by current actors

### Requirement: Bot-owned media and lookup calls stay behind wrappers
Telegram authenticated file resolution/download and Telegram media sends SHALL execute inside the Telegram wrapper. Current OneBot member, forwarded-message, file-resolution, and poke calls SHALL execute inside the OneBot wrapper. The wrappers MUST enforce existing configured byte bounds and MUST NOT expose Telegram tokenized URLs or provider clients to actors.

#### Scenario: Telegram media is fetched
- **WHEN** Bridge requests a Telegram file within its current byte limit
- **THEN** the wrapper resolves and downloads it and returns bounded bytes and metadata

#### Scenario: Telegram file exceeds its bound
- **WHEN** declared or downloaded media exceeds the request/configured maximum
- **THEN** the wrapper returns a definitely classified size failure and discards the oversized value

#### Scenario: OneBot provider-specific lookup runs
- **WHEN** Bridge requests a listed OneBot member, forward, or file operation
- **THEN** the wrapper validates the OneBot response and returns the corresponding provider-namespaced value

### Requirement: Existing process lifecycle and compatibility remain authoritative
One shared operation runtime/client SHALL be reused across actor generations around the existing process-owned bots. This change MUST add no network probe, worker thread, runtime database, outbox, rate governor, or blob store. Existing `BotRegistry` and bot interfaces MAY remain available for compatibility outside the two migrated actors, and validation-only mode MUST cause no new network or persistent side effect.

#### Scenario: Actor generation reloads
- **WHEN** a candidate actor generation is built
- **THEN** it receives the same process operation client without recreating bots or wrappers

#### Scenario: Validation-only startup runs
- **WHEN** configuration is validated without normal startup
- **THEN** installation references and surfaces are checked without constructing a new transport, calling a provider, or creating operation state

#### Scenario: Process shuts down
- **WHEN** existing runtime shutdown begins while an operation is in flight
- **THEN** current bot ownership and actor invocation lifetime rules govern completion/cancellation and the operation client adds no detached worker or dangling bot capture

### Requirement: Dispatcher coverage is limited to testable QQ and Telegram paths
Automated tests SHALL cover every advertised action with current fake bots or mock transports, including exact routing, wrong surface, unsupported action, valid success, explicit provider failure, malformed response, and uncertain side-effect exception. Tests MUST NOT require credentials or an implementation branch for any unsupported platform.

#### Scenario: Advertised-action conformance runs
- **WHEN** the Telegram and OneBot wrapper suites enumerate supported actions
- **THEN** every advertised action has an executable success test and no action outside the closed matrix is present
