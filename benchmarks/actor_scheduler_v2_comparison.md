# ActorScheduler V2 Rollout Comparison

> Historical pre-admission comparison; it does not describe the current
> supported runtime.

Recorded: 2026-07-11

This report compares the native V2 scheduler with the recorded V1 workloads
using the same optimized build, scale, and four-thread process budget. Each
value is the median of five independent process runs. The native split was
three actor workers plus one Asio I/O/completion thread; V1 used four Asio
threads. Both engines completed every invocation without an actor failure.

## Reproduction

```sh
cmake -S . -B build-bench \
  -DOBCX_BUILD_TESTS=OFF \
  -DOBCX_BUILD_BENCHMARKS=ON \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-bench --target actor_scheduler_v1_benchmark -j 4

for run in 1 2 3 4 5; do
  build-bench/benchmarks/actor_scheduler_v1_benchmark \
    --engine asio-v1 --scale 1 --threads 4
done
for run in 1 2 3 4 5; do
  build-bench/benchmarks/actor_scheduler_v1_benchmark \
    --engine native-v2 --scale 1 --threads 4
done
```

The executor's deterministic random seed was `0x4f424358`.

## Median Results

| Workload | V1 tasks/s | V2 tasks/s | V1 queue p95 ms | V2 queue p95 ms | V1 latency p95 ms | V2 latency p95 ms | V1 CPU ms | V2 CPU ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| balanced | 209474.454 | 151058.566 | 12.480 | 23.985 | 19.000 | 26.974 | 57.624 | 54.116 |
| hot-mailbox | 298195.329 | 154600.476 | 13.131 | 25.913 | 13.134 | 25.917 | 38.164 | 51.511 |
| fixed-shard-skew | 975.660 | 46291.566 | 498.058 | 9.657 | 499.073 | 10.709 | 27.639 | 11.038 |
| io-suspension | 12934.416 | 78237.325 | 33.211 | 2.771 | 34.219 | 6.517 | 11.711 | 11.860 |
| completion-storm | 2635.433 | 45709.405 | 166.915 | 3.502 | 173.035 | 11.173 | 12.709 | 13.385 |
| facade-destruction-under-load | 227497.304 | 140194.350 | 11.152 | 26.479 | 17.488 | 28.942 | 53.621 | 57.673 |

Median process peak RSS was 24,724 KiB for V1 and 25,752 KiB for V2. The V2
fixed-shard-skew runs recorded a median 213 successful steals and reduced p95
queue delay by 98.1%, well above the required 50% improvement. I/O suspension
and completion-storm latency also improved substantially.

## Gate Decision

The balanced-load gate did not pass:

- V2 balanced throughput was 72.1% of V1; the threshold is at least 90%.
- V2 balanced p95 latency was 142.0% of V1; the limit is at most 115%.
- The hot-mailbox and facade-destruction workloads also regressed, identifying
  completion delivery and single-mailbox overhead as follow-up optimization
  targets.

The correctness, skew, I/O, CPU, and memory observations do not override a
required failed gate. Therefore `asio-v1` remains the runtime default and
`native-v2` remains selectable and ready for further optimization. No default
flip is permitted by this rollout record.

## Million-Transition Stress Record

The deterministic V2 stress gate used seed `0x4f424358`, 200,000 normally
completed invocations, and 2,048 cancelled suspended invocations:

```text
transitions=1008192 completed=200000 cancelled=2048 late_completions=2048
```

Every normal task crossed two actor resumes and one external-suspension
boundary. The cancellation phase delivered exactly one cancellation per task,
then replayed stale notifications before and after scheduler destruction. No
loss, duplicate completion, hang, or post-destruction resume was observed.
