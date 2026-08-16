## 1. Define The Actor Command Message Protocol

- [x] 1.1 Add SDK types for normalized command invocation, named command request messages, transaction identity, `CommandCompleted`, and `Continue`/`Consume` propagation with nlohmann JSON coverage
- [x] 1.2 Add compile-time command catalog helpers that accept command name, description, and request message type without accepting handler names, callables, or member-function pointers
- [x] 1.3 Add positive and negative compile fixtures for valid request messages, duplicate command names, malformed declarations, and request types absent from reflected accepted inputs
- [x] 1.4 Extend `ReflectedActor` contract generation with deterministic optional command registrations while keeping actors without declarations valid

## 2. Load And Validate Command Registrations

- [x] 2.1 Extend the parsed V2 actor contract model with command registrations and add loader tests for valid, empty, malformed, duplicate, unsorted, callable-bearing, and unsupported-request contracts
- [x] 2.2 Reject command request types outside the same actor contract's accepted input set before actor construction or registration
- [x] 2.3 Update installed SDK fixtures and standalone actor contract tests to prove command registrations use the existing V2 symbol and additive schema

## 3. Add Explicit Command Configuration

- [x] 3.1 Add `command_runtime` timeout and route parsing for actor, commands, platforms, bots, and fallback with focused configuration diagnostics
- [x] 3.2 Build an immutable generation command table from config, actor contracts, bot metadata, and adapter availability
- [x] 3.3 Add validation tests for missing actors, undeclared commands, unsupported request types, missing/mismatched bots, unavailable adapters, invalid fallbacks/timeouts, and scoped route conflicts
- [x] 3.4 Run the same command validation in startup, reload candidate preparation, and `--validate-config` before bots, ingress, transactions, or external publication

## 4. Implement Platform Command Adapters

- [x] 4.1 Define `ICommandPlatformAdapter` detection, candidate catalog validation, publication capability, and aggregate reconciliation result interfaces without actor-routing methods
- [x] 4.2 Implement Telegram detection tests and behavior for command entities, exact token boundaries, arguments, and explicit bot targeting
- [x] 4.3 Implement QQ detection tests and behavior for its supported command representation without actor-local prefix parsing
- [x] 4.4 Select adapters from configured bot platform types and verify detection-only adapters keep local routing available when catalog publication is unsupported

## 5. Implement The Command Coordinator

- [x] 5.1 Add root pre-pipeline observation that passes unmatched events once and creates a generation-scoped transaction for active commands
- [x] 5.2 Construct the actor-declared typed request envelope with normalized invocation, retained source context, transaction identity, expected actor, generation, and internal reply metadata
- [x] 5.3 Submit request messages through ActorScheduler and reflected dispatch with ordinary mailbox, backpressure, cancellation, and actor failure behavior
- [x] 5.4 Extract the directed `CommandCompleted` message from the actor result, route other emitted messages normally, and reject missing, duplicate, malformed, wrong-actor, and wrong-generation completions
- [x] 5.5 Implement same-generation `Continue` and `Consume`, processed command headers, detection bypass, and exactly-once source completion
- [x] 5.6 Add bounded timeout, configured fallback, shutdown cancellation, and stable payload-free command diagnostics

## 6. Integrate Reload And Platform Catalog Lifecycle

- [x] 6.1 Retain command transactions and their routed descendants in generation drain accounting and test commands spanning successful, rejected, and timed-out reloads
- [x] 6.2 Prevent candidate preparation from mutating active command tables or external catalogs and cancel superseded generation publication retries
- [x] 6.3 Derive one deterministic active catalog per bot and implement post-activation aggregate publication, desired/observed status, and bounded retry
- [x] 6.4 Add Telegram replacement-menu tests proving commands from multiple actors are published together and publication failure does not disable local routing

## 7. Migrate Local Actors

- [x] 7.1 Migrate `obcx-actor-bridge` Telegram `/recall`, `/checkalive`, and `/poke` plus QQ `/checkalive` to typed command request messages and remove raw prefix parsing
- [x] 7.2 Make bridge completion and processed headers preserve required message-store behavior while preventing command re-execution, bridge forwarding, and the current missing-forwarding-mapping failure
- [x] 7.3 Add bridge command tests for both platforms, exact matching, application emissions, Continue/Consume, fallback, reload, and detached-work lifetime under sanitizers
- [x] 7.4 Migrate `chat_llm` `/chat` and `/toggle_think` to distinct request messages while retaining mention, reply, wake-word, proactive, and topic-send behavior on ordinary events
- [x] 7.5 Add message-store tests proving command headers survive the live `RawMessageEvent` to `MessageStored` path and document that restart replay does not preserve headers unless persistence is extended separately
- [x] 7.6 Update `obcx-actor-template` with a command observation example and keep actor-registry changes limited to optional marketplace metadata if separately chosen

## 8. Document And Verify The Cutover

- [x] 8.1 Document command declarations, route configuration, platform adapter responsibilities, completion/fallback semantics, aggregate catalogs, and migration from actor-local parsing
- [x] 8.2 Add end-to-end tests covering unmatched events, active commands, actor business emissions, Continue, Consume, conflicts, timeouts, reload drain, and validation-only startup
- [x] 8.3 Run strict OpenSpec validation, core unit/integration tests, standalone installed-SDK actor builds, and affected local actor test suites
- [x] 8.4 Run TSan and ASan/UBSan coverage for coordinator timeout/cancellation, actor suspension, catalog retry, shutdown, and repeated generation reload
- [x] 8.5 Audit runtime, SDK, platform, and migrated actor sources to confirm commands expose only message protocols and no actor independently publishes a replacement command catalog
