## 1. Reproduce the downstream interception defect

- [x] 1.1 Add a QQ bridge-pipeline regression using `/tp 2072 ~ 1080` with no active `tp` route and an enabled group mapping
- [x] 1.2 Add the equivalent Telegram regression with a valid unmatched `bot_command` entity and bot scope
- [x] 1.3 Assert message-store persistence, one target send, one queryable forwarding mapping, `MessageForwarded`, and absence of `bridge forwarding completed without persisted mapping`
- [x] 1.4 Add negative cases for an actively consumed command and an independently disabled or absent bridge mapping

## 2. Remove duplicate command interception from bridge handlers

- [x] 2.1 Remove the unconditional leading-slash early return and stale command-coordinator comment from `QQHandler::forward_to_telegram`
- [x] 2.2 Remove the equivalent leading-slash early return from `TelegramHandler::forward_to_qq`
- [x] 2.3 Preserve message-type, mapping enablement, loop prevention, de-duplication, processed-command, and retry behavior without adding a local command catalog lookup
- [x] 2.4 Ensure unmatched message text and arguments reach existing bridge formatting unchanged by command routing

## 3. Validate command and pipeline behavior

- [x] 3.1 Run command platform-adapter and command-coordinator tests, including unmatched exact and pattern candidates
- [x] 3.2 Build and run bridge handler, actor, and full message-pipeline tests against a freshly installed OBCX SDK
- [x] 3.3 Run the isolated pipeline regressions with path guards proving no production database is opened or modified
- [x] 3.4 Update bridge command-routing documentation to state that only active command routes intercept slash-prefixed traffic
- [x] 3.5 Run `openspec validate forward-unmatched-slash-messages --strict --no-interactive`
