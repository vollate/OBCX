## 1. Extend Command Declarations And Contracts

- [x] 1.1 Add an optional constexpr `command::re2(...)` matcher to `command::observe` while preserving the existing name, description, and request-message overload
- [x] 1.2 Generate deterministic optional matcher kind, pattern, and full-match mode metadata in the existing schema-1 actor contract
- [x] 1.3 Add positive and negative compile fixtures for commands with no matcher, one valid matcher, malformed matcher declarations, and unchanged canonical-name validation

## 2. Parse And Validate RE2 Matcher Metadata

- [x] 2.1 Extend parsed actor command registrations with an optional matcher description without adding handler, callable, or platform fields
- [x] 2.2 Validate matcher shape, supported RE2 kind/full mode, non-empty pattern, fixed byte limit, bounded RE2 options, and compilation before actor construction
- [x] 2.3 Add actor-manager and standalone installed-SDK tests for valid additive metadata, invalid RE2 syntax, unsupported matcher members, and resource-limit failures

## 3. Produce Bounded Platform Command Candidates

- [x] 3.1 Separate platform-valid normalized candidate tokens from the stricter canonical command-name rules so adapters can expose bounded aliases
- [x] 3.2 Preserve Telegram command-entity, token-boundary, and exact bot-target validation before matcher evaluation
- [x] 3.3 Preserve QQ leading-command parsing while adding deterministic candidate normalization and maximum input length coverage

## 4. Build Generation-Owned Pattern Routes

- [x] 4.1 Precompile matchers for activated canonical commands into immutable per-generation bot routing state using fixed UTF-8, no-log, and memory options
- [x] 4.2 Reject invalid compiled state and identical active patterns on the same platform/bot scope during startup, reload candidate preparation, and `--validate-config`
- [x] 4.3 Keep existing command configuration keyed by canonical string names and require no configuration migration for commands without matchers

## 5. Route Exact And Pattern Matches Deterministically

- [x] 5.1 Keep exact canonical route lookup authoritative and evaluate RE2 full matches only when no exact active route exists
- [x] 5.2 Route one unique pattern match through the existing typed request using the canonical invocation name and unchanged adapter-parsed arguments
- [x] 5.3 Report `command_match_ambiguous` for multiple pattern matches, invoke no actor, apply no route fallback, and send the original event through ordinary routing exactly once
- [x] 5.4 Preserve canonical processed headers, completion correlation, `Continue`/`Consume`, timeout, fallback, and business-emission behavior for regex-selected commands

## 6. Preserve Catalog, Reload, And Diagnostic Boundaries

- [x] 6.1 Publish only one canonical name and description per activated command without exposing patterns or inferred aliases to platform catalogs
- [x] 6.2 Retain compiled matcher state with its admitting generation across successful, rejected, and draining reloads and destroy it with the retired route table
- [x] 6.3 Add bounded matcher validation and ambiguity diagnostics that omit pattern text, candidate tokens, arguments, raw payloads, and credentials

## 7. Document And Verify The Extension

- [x] 7.1 Document canonical-name stability, normalized-candidate full matching, exact precedence, ambiguity behavior, catalog exclusion, and matcher resource limits
- [x] 7.2 Update the actor command example with one optional RE2 alias while keeping handler selection exclusively on the typed request message
- [x] 7.3 Add coordinator tests for unique alias matches, no substring matches, exact precedence, identical-pattern validation, overlapping-pattern ambiguity, and ordinary-route exactly-once behavior
- [x] 7.4 Add runtime-generation tests for validation-only startup, aggregate catalog publication, reload cutover, and old-generation matcher retention
- [x] 7.5 Run command adapter/coordinator, actor contract, runtime generation/reload, standalone SDK/local actor, TSan, and ASan/UBSan coverage
- [x] 7.6 Run repository formatting and whitespace checks and validate this OpenSpec change strictly with all artifacts complete
