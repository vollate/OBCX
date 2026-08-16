## Why

Exact canonical command names are sufficient for one spelling, but they cannot
express aliases or other bounded command-token forms without duplicating actor
registrations. OBCX should let one command optionally recognize an RE2 pattern
while preserving its ordinary string name as the stable routing, catalog, and
actor-message identity.

## What Changes

- Add an optional RE2 match pattern to an actor command observation; the
  canonical command `name` remains a required ordinary string.
- Extend generated and parsed actor command metadata with an additive optional
  matcher description that contains no handler or callable information.
- Match the RE2 pattern only against the platform adapter's normalized command
  name after platform syntax and explicit bot targeting have been validated.
- Keep exact canonical-name matching authoritative. If there is no exact active
  match, route one unique pattern match to that command's existing typed request
  message and report ambiguous pattern matches without choosing by declaration
  or configuration order.
- Validate RE2 syntax and resource limits before actor activation, precompile
  active patterns per runtime generation, and reject invalid candidates during
  startup, reload preparation, and `--validate-config`.
- Continue publishing only canonical command names and descriptions to platform
  catalogs; regex aliases do not become remote menu entries.
- Preserve the existing `CommandInvocation`, `CommandCompleted`,
  `Continue`/`Consume`, timeout, fallback, diagnostics, and reload semantics.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `actor-command-routing`: Allow an activated canonical command to opt into a
  deterministic RE2 matcher over normalized command names.
- `actor-abi-v2`: Add optional RE2 matcher metadata to command registrations
  without changing the ABI symbol, actor handler model, or canonical command
  identity.

## Impact

- Public actor SDK command declaration helpers and generated
  `obcx_get_actor_contract` command metadata.
- Actor contract parsing and validation, command route configuration/building,
  generation-owned matching, safe diagnostics, and command routing tests.
- The core runtime's existing RE2 dependency; no new regex engine or actor-local
  regex execution is introduced.
- This change depends on `add-actor-command-routing` and must be applied after
  that capability becomes the active baseline.
