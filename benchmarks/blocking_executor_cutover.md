# Blocking Executor Cutover Benchmark

Date: 2026-07-30

This benchmark compares the event-driven `BlockingExecutor` completion path
with a benchmark-only reproduction of the retired future bridge: wait on the
future for 1 ms, then await a 1 ms Asio timer before polling again. It also
holds one actor partition inside `run_blocking` while measuring progress across
20,000 independent partitions on a single actor worker.

The polling baseline exists only in the benchmark target. Production sources
and installed SDK headers are separately audited to contain no future/timer
polling bridge or retired bot scheduling API.

## Reproduction

```bash
cmake -S . -B build/actor-dev -DOBCX_BUILD_BENCHMARKS=ON
cmake --build build/actor-dev --target blocking_executor_benchmark -j2
build/actor-dev/benchmarks/blocking_executor_benchmark \
  --iterations 500 --work-us 1000 --partitions 20000
```

Environment: Debug build, GCC 16.1.0, 12th Gen Intel Core i7-12700 (20 logical
CPUs), branch base `0ebdb21`. The table reports the median value from three
consecutive runs.

| Path | Mean completion | p50 | p95 | Throughput |
| --- | ---: | ---: | ---: | ---: |
| Event-driven executor | 1,110.88 us | 1,097.86 us | 1,188.69 us | 900.06 ops/s |
| Retired polling baseline | 2,050.54 us | 2,075.72 us | 2,090.41 us | 487.65 ops/s |

At a 1 ms synchronous workload—the old polling interval boundary—the
event-driven path reduced mean completion latency by 45.8% and increased
sequential completion throughput by 84.6%.

The independent-partition median was 65.99 ms for 20,000 messages
(303,087 ops/s). `same_partition_started_before_release=false` in all three
runs: the held mailbox remained exclusive while unrelated partitions
continued to execute.

These figures are a local comparison, not a cross-host capacity promise. Keep
the command, compiler mode, workload, and host fixed when comparing future
runtime changes.
