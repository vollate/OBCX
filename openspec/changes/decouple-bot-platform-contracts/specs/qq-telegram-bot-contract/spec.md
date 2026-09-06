## MODIFIED Requirements

### Requirement: Bot references are scoped to an implemented installation
The bot-operation contract SHALL identify a configured bot by `BotInstallationRef` containing an exact installation id and an explicit `SurfaceId`. Group and message references MUST contain that installation, and Telegram message references MUST carry chat id and message id as separate fields. Shared reference syntax MUST be platform independent; module/configuration and endpoint validation SHALL require an actually registered exact surface. The production modules SHALL remain limited to `telegram.bot_api` and `onebot11.qq`; OneBot 11 MUST NOT be represented as `qq.official`.

#### Scenario: Equal ids belong to different installations
- **WHEN** two configured bots report the same native group or message id
- **THEN** their scoped references compare as different resources

#### Scenario: Telegram delete is represented
- **WHEN** Telegram message `42` in chat `-1001` is targeted for deletion
- **THEN** the request contains installation, chat `-1001`, and message `42` as separate values rather than a colon-encoded string

#### Scenario: Current QQ adapter is referenced
- **WHEN** a request targets the implemented QQ-compatible bot
- **THEN** its exact surface is `onebot11.qq` and no field implies official QQ support

#### Scenario: Unregistered surface reaches production
- **WHEN** a syntactically valid reference names a surface absent from the production catalog
- **THEN** configuration or route validation rejects it before provider I/O rather than selecting another platform

### Requirement: The first operation set is closed to current QQ and Telegram calls
The production contracts SHALL define only `message.send_group`, `message.delete`, `telegram.message.send_topic`, `telegram.message.edit_text`, `telegram.media.send_photo`, `telegram.media.send_group_urls`, `telegram.media.send_group_uploads`, `telegram.media.fetch_file`, `onebot11.group_member.get`, `onebot11.forward_message.get`, `onebot11.group_file.resolve`, `onebot11.private_file.resolve`, and `onebot11.group.poke` in this change. Common send/delete declarations SHALL belong to the common SDK, and provider-specific declarations SHALL belong to their owning platform SDK. This set MUST NOT be encoded as a central enum, ordinal lookup, common virtual overload list, or global compatibility matrix. Production modules MUST NOT add portable history, contact, moderation, private-send, reaction, poll, membership-list, request-handling, credential, or unsupported-platform operations.

#### Scenario: Current Bridge operation is represented
- **WHEN** Bridge needs one of the listed QQ or Telegram calls
- **THEN** it constructs the corresponding platform/common typed request without a live bot object or an all-platform umbrella client

#### Scenario: Unused legacy method is requested
- **WHEN** a caller attempts an `IBot` action outside the current production list
- **THEN** no production endpoint advertises or executes it and an envelope naming it fails before provider I/O

#### Scenario: Another platform is considered
- **WHEN** production code attempts an operation for Discord, official QQ, WeChat, WeCom, Lark, DingTalk, Matrix, X, or another unregistered surface
- **THEN** production catalog/routing validation rejects the surface and no compatibility alias maps it to Telegram or OneBot

#### Scenario: Modularity is tested without adding a provider
- **WHEN** an isolated generic-runtime fixture registers a synthetic module and typed action
- **THEN** it can prove registration extensibility without modifying the production action set or adding a real-platform implementation

### Requirement: Public values serialize deterministically
Every public installation, target, message reference, request, success value, and error SHALL provide deterministic JSON conversion and validation suitable for independently built actor packages. Existing production surface/action strings and DTO field semantics SHALL remain unchanged. Shared IDs SHALL validate syntax without enumerating platforms; platform typed request codecs and endpoint adapters SHALL validate allowed surfaces, required routing, non-positive Telegram topic ids, and declared media bounds. The common serialization layer MUST NOT import other platforms to perform those checks.

#### Scenario: Value crosses an installed SDK boundary
- **WHEN** a valid request or result is serialized and deserialized by a standalone single-platform actor fixture
- **THEN** all routing, payload, result, error, and provider-specific fields are preserved using only that platform and common SDK

#### Scenario: Surface and action disagree
- **WHEN** a OneBot installation is paired with a `telegram.*` request
- **THEN** the Telegram typed codec or server-side adapter rejects it before provider I/O even if a caller bypasses the typed facade

#### Scenario: Existing DTO golden fixtures are replayed
- **WHEN** recorded production request and result JSON values are round-tripped through the new platform-owned contracts
- **THEN** their stable IDs, payload fields, exact references, and submission-safety values retain the recorded semantics
