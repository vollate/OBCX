## 1. Make Reflected Dispatch Generation-Safe

- [x] 1.1 Add a repeated staged-DSO reload regression that initializes reflected dispatch before later actor generations are loaded
- [x] 1.2 Remove the actor-specific function-local static dispatch table and directly select the current generation's exact reflected handler
- [x] 1.3 Preserve unsupported-type, JSON decoding, sync/async normalization, task runtime attachment, and decoded-input lifetime behavior

## 2. Close Reload Ingress Races And Surface Failures

- [x] 2.1 Make gate waiter wakeups persistent across the waiter publish and `async_wait` registration race
- [x] 2.2 Check bot ingress results and log structured first-failure routing diagnostics without payload contents
- [x] 2.3 Exercise messages before gate closure, while the gate is closed, and after cutover through the real event-dispatch path

## 3. Verify The Applied Fix

- [x] 3.1 Pass the reflected actor unit suite and reload-controller suite
- [x] 3.2 Pass the installed bridge/message-store repeated reload smoke with ingress-result and database-mapping assertions
- [x] 3.3 Pass the reload-controller ThreadSanitizer suite without a sanitizer report
- [x] 3.4 Pass repository formatting and diff whitespace checks
- [x] 3.5 Validate this OpenSpec change strictly with all artifacts complete
