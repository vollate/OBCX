# ActorScheduler V1 Baseline

> Historical comparator captured before the actor-only cutover. It does not
> describe a supported runtime.

Recorded: 2026-07-11

This baseline is the Phase 0 comparison point for OpenSpec change
`native-actor-coroutine-scheduler`. It records the fixed-shard V1 scheduler
before native ActorTask execution is enabled.

## Environment

- OS: Linux 7.1.3-arch1-1 x86_64
- CPU: Intel Core Ultra 5 125H, 18 logical CPUs
- Compiler: GCC 16.1.1
- CMake: 4.4.0
- Boost: 1.91
- Build type: `RelWithDebInfo`
- Benchmark Asio runner threads: 4
- Benchmark scale: 1

`RelWithDebInfo` is used because the repository currently forces CMake unity
builds for `Release`, and two pre-existing anonymous-namespace helpers collide
when core sources are combined into one unity translation unit. The benchmark
itself is otherwise optimized.

## Reproduction

```sh
cmake -S . -B build-bench \
  -DOBCX_BUILD_TESTS=OFF \
  -DOBCX_BUILD_BENCHMARKS=ON \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-bench --target actor_scheduler_v1_benchmark -j 4
build-bench/benchmarks/actor_scheduler_v1_benchmark --scale 1
```

The executable prints CSV. Increase `--scale` for longer comparison runs. Peak
RSS is process high-water memory and is therefore cumulative across workloads.
Queue delay is measured from workload release until an actor handler begins;
latency is measured until the scheduler result callback completes.

## Recorded Results

| Workload | Tasks | Failures | Wall ms | CPU ms | Tasks/s | Peak RSS KiB | Queue p95 ms | Queue p99 ms | Latency p95 ms | Latency p99 ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| balanced | 4096 | 0 | 15.759 | 45.235 | 259909.626 | 67768 | 13.134 | 13.586 | 13.294 | 15.519 |
| fixed-shard-skew | 512 | 0 | 518.338 | 338.511 | 987.772 | 67768 | 491.312 | 511.657 | 492.873 | 513.274 |
| io-suspension | 512 | 0 | 42.407 | 64.609 | 12073.360 | 67768 | 35.192 | 39.143 | 37.022 | 41.067 |
| completion-storm | 512 | 0 | 192.280 | 214.026 | 2662.784 | 67768 | 165.770 | 180.996 | 171.282 | 186.797 |
| facade-destruction-under-load | 4096 | 0 | 36.323 | 100.018 | 112765.777 | 67768 | 30.837 | 31.703 | 35.146 | 35.805 |

The skew workload deliberately selects 64 distinct partition keys that all hash
to V1 shard zero and makes each handler await 1 ms. Its roughly 0.5 second wall
time captures the same-shard head-of-line behavior that V2 must remove.

## V2 Rollout Gates

Correctness gates are absolute:

- zero lost, duplicated, or failed benchmark invocations;
- same-mailbox maximum concurrency remains one;
- unrelated mailboxes that share a V1 shard hash run independently on V2;
- no shutdown hang, late resume, or post-destruction callback;
- all sanitizer, core, standalone actor, and cross-repository tests pass.

Performance gates compare at the same build type, scale, thread budget, and
otherwise idle host. Use the median of at least five runs:

- balanced throughput must remain at least 90% of V1 and balanced p95 latency
  must not exceed 115% of V1;
- fixed-shard-skew p95 queue delay must improve by at least 50%;
- I/O-suspension and completion-storm p95 latency must not exceed 115% of V1;
- CPU time for any workload must not exceed 120% of V1 unless wall latency
  improves by at least the same percentage;
- peak RSS must remain within the greater of 125% of V1 or V1 plus 32 MiB;
- cancelling and graceful shutdown workloads must finish within their declared
  timeout and leave no tracked work.

If any required gate fails, `asio-v1` remains the default and the failing CSV,
seed, system load, and scheduler metrics must be retained for diagnosis.
