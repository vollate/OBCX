## Why

The command coordinator correctly sends a slash-prefixed message to ordinary
routing when its normalized command name has no active route. The bridge's QQ
and Telegram forwarding handlers then independently reject every message whose
raw text begins with `/`. An unregistered message such as
`/tp 2072 ~ 1080` is therefore persisted by `message_store` but never reaches
the target bot.

After the handler returns without a mapping, `BridgeForwardingRuntime` reports
`bridge forwarding completed without persisted mapping`, which makes an
intentional legacy prefix filter appear to be a persistence failure. This
violates the command-routing requirement that an unmatched candidate enter its
ordinary pipeline once and makes the active command catalog ineffective as the
authority for interception.

## What Changes

- Make the generation command coordinator and its active routing table the
  only authority that decides whether a leading slash denotes an intercepted
  command.
- Remove the unconditional `raw_message.starts_with("/")` suppression from
  both QQ-to-Telegram and Telegram-to-QQ bridge forwarding handlers.
- Forward an unmatched slash-prefixed message through the same configured
  bridge path as any other business message, including sender formatting, bot
  send, mapping persistence, and `MessageForwarded` publication.
- Preserve active-command behavior: a consumed command never enters ordinary
  routing, while existing processed-command handling remains controlled by
  coordinator metadata rather than reparsing raw text downstream.
- Preserve independent bridge filters for disabled mappings, unsupported
  message types, loop prevention, de-duplication, and explicit forwarding
  policy.
- Add QQ and Telegram regression tests using the complete command coordinator,
  message-store, bridge, and mock-bot pipeline, plus focused handler tests that
  prove slash syntax alone cannot suppress a message.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `actor-command-routing`: Clarify that unmatched slash-prefixed candidates
  remain ordinary business traffic throughout downstream pipelines and cannot
  be intercepted again by bridge-specific prefix checks.

## Impact

- `local_actor/obcx-actor-bridge` QQ and Telegram forwarding handlers and
  their tests.
- Root command-routing and actor-pipeline integration tests using isolated
  databases and mock bot transports.
- No actor ABI, configuration, command catalog, database schema, or stored-data
  migration is required.
- The separate `eliminate-bridge-mapping-roundtrips` change may improve how
  legitimate non-forwarded outcomes are represented, but this change does not
  depend on it.
- Mapping-roundtrip optimization, Redis buffering, asynchronous persistence,
  and changes to registered-command propagation policy are out of scope.
