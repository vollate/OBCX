# C++26 Reflected Routing-Context Benchmark

Recorded on 2026-07-14 on Linux x86_64 with the repository's admitted GCC
16.1 Nix toolchain and a Debug build. The benchmark exercises the real
orchestrator, native actor scheduler, recursive emitted-message routing, and a
single 32-way fan-out at the midpoint of a 16-hop route.

```text
$ build/cpp26-dev/benchmarks/actor_routing_context_benchmark \
    --depth 16 --fan-out 32 --iterations 100
route_context_storage=copy-on-write-linked
depth=16
fan_out=32
iterations=100
stage_executions=26500
elapsed_ns=528626674
ns_per_stage=19948
```

The route context uses an immutable shared parent chain. Creating a sibling
snapshot is therefore O(1) and does not copy its ancestors. Cycle lookup walks
only the configured, finite route, while failure rendering includes at most
the last eight `(pipeline, stage, message_type)` nodes. This preserves branch
isolation without returning to the former silent recursion-depth guard.

Reproduce with:

```bash
nix develop --command cmake --build build/cpp26-dev \
  --target actor_routing_context_benchmark
build/cpp26-dev/benchmarks/actor_routing_context_benchmark \
  --depth 16 --fan-out 32 --iterations 100
```
