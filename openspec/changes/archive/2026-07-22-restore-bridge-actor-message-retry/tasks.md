## 1. Specify And Reproduce The Actor-Mode Gap

- [x] 1.1 Add a QQ-to-Telegram actor forwarding test where the initial send fails with retry enabled and assert that one durable retry row is created
- [x] 1.2 Add the equivalent Telegram-to-QQ actor forwarding test, including the platform-specific response shape used by a later successful retry
- [x] 1.3 Add tests proving retry-disabled forwarding creates no worker or row and is the only path that emits the "未启用重试" diagnostic
- [x] 1.4 Add actor configuration tests for valid retry policy extraction and rejection of zero, negative, or inconsistent interval values

## 2. Wire The Generation-Owned Retry Worker

- [x] 2.1 Add a managed worker owner for the retry `io_context`, work guard, thread, and `RetryQueueManager`
- [x] 2.2 Construct one worker from `BridgeForwardingRuntime` when `enable_retry_queue` is true and pass its manager to both forwarding handlers
- [x] 2.3 Register Telegram group/topic and QQ group resend callbacks through the process-owned `BotRegistry`
- [x] 2.4 Parse platform response message ids, persist successful mappings, and retain failed attempts without logging message contents or credentials
- [x] 2.5 Report retry initialization/unavailability separately from an explicitly disabled queue

## 3. Apply Persistent Retry Policy

- [x] 3.1 Replace compiled scheduling constants with the immutable bridge retry policy for base interval, queue check interval, and maximum interval
- [x] 3.2 Restore persisted rows before starting the worker and preserve stored attempt counts and maxima across restart
- [x] 3.3 Verify duplicate enqueue identity, retry-count updates, terminal exhaustion, successful mapping writes, and queue-row cleanup through repository tests
- [x] 3.4 Add failure-injection coverage for mapping persistence and retry-row cleanup so database failures are not reported as completed delivery

## 4. Make Reload And Shutdown Safe

- [x] 4.1 Add an idempotent, non-throwing bridge forwarding shutdown path that stops admission, cancels on the worker executor, drains callbacks, and joins the worker thread
- [x] 4.2 Release retired scheduler and actor-manager references while the generation's services, executor, and actor DSO remain valid
- [x] 4.3 Keep the reload ingress gate closed until the retired bridge worker has stopped, then allow the candidate worker to initialize lazily
- [x] 4.4 Add deterministic tests for shutdown during a timer wait, shutdown during an in-flight resend, reload with persisted pending rows, and repeated start/stop cycles
- [x] 4.5 Run TSan and ASan/UBSan lifecycle coverage and prove no retry callback, timer, worker thread, actor reference, or DSO keepalive survives generation retirement

## 5. Verify And Document The Actor-Only Result

- [x] 5.1 Run bridge unit and integration tests plus the root actor-runtime reload and shutdown suites
- [x] 5.2 Update bridge actor configuration and operations documentation with retry defaults, validation rules, at-least-once behavior, and diagnostics
- [x] 5.3 Extend the actor-only audit to prove no plugin lifecycle, global plugin bot lookup, or plugin retry owner was reintroduced
- [x] 5.4 Validate this OpenSpec change strictly and record the focused validation commands and outcomes

## Validation Record

- `ctest --test-dir build -R 'BridgeActorTest|RetryQueueManagerTest|BridgeHandlerRepositoryTest' --output-on-failure`: 31/31 passed.
- `ctest --test-dir build -R 'RuntimeReloadControllerTest|NativeActorSchedulerTest|RuntimeGenerationTest' --output-on-failure`: 30/30 passed.
- `build/tests/standalone_actor_reload_smoke build/actors/message_store.so build/actors/bridge.so`: passed with `reload=ok old_routes=1 new_routes=3 bot_reconnects=0`.
- TSan lifecycle selection: 22/22 passed; the installed actor reload smoke also passed with the same reload result.
- ASan/UBSan lifecycle selection: 22/22 passed; the installed actor reload smoke also passed with the same reload result. Leak detection was disabled because LeakSanitizer cannot operate under the ptrace-based test sandbox; address and undefined-behavior checks remained enabled with halt-on-error.
- `openspec validate restore-bridge-actor-message-retry --strict --no-interactive`: passed.
