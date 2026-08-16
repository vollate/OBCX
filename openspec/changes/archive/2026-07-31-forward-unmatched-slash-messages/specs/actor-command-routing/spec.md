## ADDED Requirements

### Requirement: Unmatched slash-prefixed messages remain ordinary traffic

The generation command coordinator SHALL be the sole authority that converts a
platform command candidate into an intercepted command transaction. When a
candidate has no active route for its platform and bot scope, downstream
pipeline actors MUST treat the original event as ordinary business traffic and
MUST NOT suppress or consume it solely because its raw text begins with `/`.
In particular, a bridge forwarding handler MUST NOT use a raw leading-slash
check as a substitute for the active command routing table.

#### Scenario: An unregistered QQ command-shaped message is bridged

- **WHEN** QQ receives `/tp 2072 ~ 1080`, no active QQ route matches `tp`, and the source group has an enabled bridge mapping
- **THEN** the original message traverses the ordinary message-store and bridge stages once, the target bot is called once, and one forwarding mapping is produced

#### Scenario: An unregistered Telegram command entity is bridged

- **WHEN** Telegram receives a valid leading `bot_command` entity whose normalized name has no active route for that bot and the source group has an enabled bridge mapping
- **THEN** the original message traverses the ordinary pipeline and is forwarded once instead of being rejected by its leading slash

#### Scenario: Slash syntax appears without a platform command match

- **WHEN** a platform adapter reports no command candidate for a slash-prefixed event
- **THEN** downstream bridge processing applies only its ordinary routing, loop, de-duplication, and forwarding rules

#### Scenario: An active command is consumed

- **WHEN** a slash-prefixed event matches an active command route whose valid completion selects `consume`
- **THEN** the command coordinator completes the source operation without submitting it to the ordinary bridge pipeline

#### Scenario: An independent bridge rule rejects the message

- **WHEN** an unmatched slash-prefixed event reaches a bridge whose group mapping is disabled or absent
- **THEN** the bridge may skip it for that explicit routing policy but not because of the slash prefix

### Requirement: Unmatched-command coverage reaches terminal pipeline effects

Automated command-routing coverage SHALL verify the terminal effects of an
unmatched slash-prefixed event across the configured ordinary pipeline, not
only that the coordinator invoked a generic orchestrator. The coverage MUST
use isolated persistence and mock external bot transports.

#### Scenario: The unmatched QQ regression completes

- **WHEN** the end-to-end actor pipeline processes the unregistered QQ message `/tp 2072 ~ 1080`
- **THEN** it verifies message persistence, exactly one target send, a queryable source-to-target mapping, successful forwarding completion, and no missing-mapping bridge failure

#### Scenario: A matched command regression completes

- **WHEN** the same pipeline processes a command that is actively routed and consumed
- **THEN** it verifies the command actor is invoked while the bridge target bot is not called
