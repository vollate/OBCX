## Why

After several successful actor-runtime reloads, a correctly typed bot message
can reach the new message-store generation and still fail with
`unsupported_message_type`. The reflected dispatcher previously cached
actor-specific canonical-name views and dispatch function pointers in a
function-local static table. GCC emits that storage with GNU-unique linkage,
so separately staged copies of the same actor DSO can reuse metadata owned by
an already retired generation.

The failure was also easy to miss operationally because bot ingress discarded
the returned `OrchestratorResult`. In addition, opening the reload ingress gate
used a bare timer cancellation whose wakeup could be lost when it raced with
wait registration.

## What Changes

- Replace the function-local reflected dispatch table with generated direct
  handler selection that constructs the matching task from the current actor
  generation and retains no actor-specific cross-generation function pointers
  or string views.
- Make reload-gate wakeups persistent by expiring published waiter timers, so
  opening the gate works whether the asynchronous wait was registered before
  or immediately after the wakeup was posted.
- Check bot ingress `OrchestratorResult` values and log the first structured
  actor failure, including routing location, stable code, and retryability,
  instead of silently discarding failed routes.
- Extend the installed actor reload smoke test to use the real event dispatcher
  and one shared bot task scheduler across repeated cold and active reloads,
  including messages before gate closure, while the gate is closed, and after
  cutover.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `reflected-actor-dispatch`: Require generated dispatch state to remain local
  to the currently loaded actor generation across repeated staged DSO reloads.
- `actor-runtime-reload`: Require persistent gate wakeups, lossless real bot
  ingress across repeated reloads, and visible structured ingress failures.

## Impact

- Public reflected actor dispatch implementation in
  `include/core/actor/reflected_actor.hpp`; actor source and ABI contracts do not
  change.
- Reload ingress waiter coordination in
  `src/core/runtime/actor_runtime_reload_controller.cpp`.
- Bot event ingress diagnostics in `src/app/main.cpp`.
- Installed bridge/message-store reload coverage in
  `tests/cpp/standalone_actor_reload_smoke.cpp`.
- No configuration migration, actor rebuild requirement, or compatibility
  layer is introduced.
