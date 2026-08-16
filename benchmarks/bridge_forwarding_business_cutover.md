# Bridge Forwarding Business Simulation

Date: 2026-08-03

This benchmark measures the complete successful QQ-to-Telegram actor pipeline
with only the external bot layer mocked. It compares the current working tree
with clean pre-cutover revisions:

- OBCX core: `0ebdb21240f3edcebd82db05dbb246a17026a884`
- bridge actor: `e1b2130e968b832bb27dc818fa236cd1b0f73f40`
- message-store actor: `269dea504aa9b887492ecdcae50a8486a72a0b83`

Both versions compile the same
`bridge_forwarding_business_benchmark.cpp` source in Debug mode with GCC
16.1.0. The baseline actors and core are built from clean `git archive`
trees, so no current actor or SDK object can be loaded accidentally.

## Scope

The following production components are real:

- `RuntimeGenerationBuilder`, its resolved process thread budget, actor
  scheduler, actor I/O pool, partition routing, and two-stage orchestrator;
- dynamically loaded `message_store.so` and `bridge.so`;
- `DbManager`, its SQLite connection and writer thread, WAL mode, schema
  migration, message persistence, user cache, reply lookup, and bridge mapping
  persistence;
- bridge configuration parsing, group mapping, message adaptation, sender
  formatting, reply formatting, forwarding runtime, and retry worker
  lifecycle.

Only bot-side external calls are mocked:

- QQ group-member lookup returns deterministic member JSON;
- Telegram group send validates the destination/message and returns a unique
  successful message id.

The measured workload mirrors the active shape in
`dev/onebot/config/bridge_actor.toml`: stealing scheduler with automatic worker
counts, `source_platform:conversation_id` partitions, one shared `main` SQLite
database, message-store then bridge pipeline, successful group-to-group
forwarding, and retry enabled. It uses eight active group mappings, 64 senders,
20% reply messages, 512 warm-up messages, and concurrency 256.

Every completed run verifies the real database row counts and mock send count.
No forwarding failure is included in the timing.

## Round-trip elimination rerun (2026-08-03)

The mapping-persistence change was measured against the pre-change bridge
actor at repository revision
`53d283f988230151ea929d0cf691e3dbf0d8a6b6`. The candidate is the working tree
that propagates the bot response target id to `BridgeActor` and commits the
direct mapping there once. Both sides used the same current OBCX core,
message-store actor, benchmark executable, configuration, and mock bots. The
baseline and candidate bridge DSOs were placed in separate actor directories
so runtime actor discovery could not select the wrong artifact.

The extended benchmark wraps the configured SQLite provider and counts the
primary mapping SQL operations. It separates the known pre-send
de-duplication SELECT for every message and the expected reply mapping SELECTs
from any remaining post-send recovery SELECTs. Every run drains the full
pipeline and then verifies message rows, mapping rows, and mock sends before
printing results.

The tested artifacts were:

- pre-change bridge SHA-256:
  `43e31c5194cc7032f73f9400d5efde8f0661ae3f3c6126ff43b138b55048aac0`;
- candidate bridge SHA-256:
  `b7c385d5c592d56cf750ba5e782666d284ec7529abc9772d76d890556ac85f47`;
- shared message-store SHA-256:
  `b32a9ebf00a12cf07389324d6ddee3042d46e8bcd826067988a66f49995e133e`;
- instrumented benchmark SHA-256:
  `59d20bd230d6ad3fa60bfdf2f546cc0ef170adbae1ccac8989a8d47ba766ef73`.

### Controlled tmpfs, 10,000 measured messages

The table reports the median of three alternating baseline/candidate pairs.
Each invocation also sends eight seed and 512 warm-up messages, so operation
and final-row counts cover 10,520 completed messages.

| Metric | Pre-change | Candidate | Change |
| --- | ---: | ---: | ---: |
| Elapsed | 1,537.12 ms | 1,279.94 ms | -16.73% |
| Mean end-to-end latency | 26,048.5 us | 21,837.5 us | -16.17% |
| p50 end-to-end latency | 27,486.6 us | 24,066.8 us | -12.44% |
| p95 end-to-end latency | 37,800.8 us | 32,094.2 us | -15.10% |
| Throughput | 6,505.69 msg/s | 7,812.89 msg/s | **+20.09%** |
| Final message rows | 10,520 | 10,520 | equal |
| Final mapping rows | 10,520 | 10,520 | equal |
| Pre-send de-duplication SELECTs | 10,520 | 10,520 | equal |
| Reply mapping SELECTs | 2,102 | 2,102 | equal |
| Post-send recovery SELECTs | 10,520 | **0** | **-100%** |
| Primary mapping upserts | 21,040 | **10,520** | **-50%** |

### Production-filesystem capacity run, 10,000 measured messages

One isolated Btrfs/NVMe pair used
`build/benchmark-sandboxes/`; no production database path or credentials were
loaded.

| Metric | Pre-change | Candidate | Change |
| --- | ---: | ---: | ---: |
| Elapsed | 166,533 ms | 149,568 ms | -10.19% |
| Mean end-to-end latency | 3,142,450 us | 2,854,550 us | -9.16% |
| p50 end-to-end latency | 3,649,230 us | 3,413,100 us | -6.47% |
| p95 end-to-end latency | 4,987,580 us | 4,040,130 us | -19.00% |
| Throughput | 60.048 msg/s | 66.8592 msg/s | **+11.34%** |
| Final message rows | 10,520 | 10,520 | equal |
| Final mapping rows | 10,520 | 10,520 | equal |
| Post-send recovery SELECTs | 10,520 | **0** | **-100%** |
| Primary mapping upserts | 21,040 | **10,520** | **-50%** |

The deterministic result is the operation-count reduction: one successful
direct delivery now has one pre-send de-duplication read, no recovery read, and
one mapping upsert. The three tmpfs pairs show a stable throughput improvement.
The single Btrfs pair points in the same direction, but filesystem variability
means it remains capacity evidence rather than a repeatability claim. SQLite's
writer-thread submission and the actor/blocking-executor completion hops remain
on the path; the redundant repository read and write do not.

The remainder of this document preserves the 2026-07-30 actor cutover evidence
for historical comparison.

## Database safety

The production configuration is used only as a structural reference. Bot
credentials and the production `bridge_bot.db` path are never loaded.

Each invocation creates a uniquely named
`obcx-bridge-business-benchmark-*` directory below the selected sandbox
parent. Both `[db.instances.main].path` and the legacy bridge
`database_file` option point to the unique database inside that directory. A
runtime guard rejects the process working directory's `bridge_bot.db`, and the
benchmark removes only its validated, uniquely prefixed sandbox.

The Btrfs runs use the Git-ignored
`build/benchmark-sandboxes/` parent. This is on the same Btrfs/NVMe filesystem
as the production database without sharing its path. The directory was empty
after all runs.

## Results

### Controlled tmpfs, 10,000 messages

The table reports the median of three runs.

| Metric | Clean baseline | Current | Current change |
| --- | ---: | ---: | ---: |
| Elapsed | 1,531.04 ms | 1,628.75 ms | +6.38% |
| Mean end-to-end latency | 24,057.3 us | 27,070.1 us | +12.52% |
| p50 end-to-end latency | 23,666.1 us | 28,573.6 us | +20.74% |
| p95 end-to-end latency | 40,618.5 us | 40,383.4 us | -0.58% |
| Throughput | 6,531.50 msg/s | 6,139.68 msg/s | **-6.00%** |

The controlled in-memory filesystem removes most storage noise. The new
blocking boundary keeps p95 essentially flat, but its additional executor and
completion hops reduce successful-route throughput and median latency.

### Production-filesystem capacity run, 10,000 messages

One paired capacity run was retained because each side takes roughly three
minutes with the default SQLite durability behavior.

| Metric | Clean baseline | Current | Current change |
| --- | ---: | ---: | ---: |
| Elapsed | 164,558 ms | 185,881 ms | +12.96% |
| Mean end-to-end latency | 3,216,270 us | 3,514,080 us | +9.26% |
| p50 end-to-end latency | 3,607,350 us | 3,910,490 us | +8.40% |
| p95 end-to-end latency | 5,038,750 us | 5,014,750 us | -0.48% |
| Throughput | 60.77 msg/s | 53.80 msg/s | **-11.47%** |

This pair also shows no bridge throughput improvement. As on tmpfs, p95 is
effectively unchanged.

### Production-filesystem repeatability check, 2,000 messages

Four A/B pairs were run in alternating order. Raw throughput shows that Btrfs
and SQLite flush behavior is large enough to reverse the ranking:

| Pair | Baseline msg/s | Current msg/s | Current change |
| --- | ---: | ---: | ---: |
| 1 | 78.70 | 116.35 | +47.84% |
| 2 | 53.73 | 49.78 | -7.34% |
| 3 | 114.66 | 113.49 | -1.02% |
| 4 | 50.57 | 121.32 | +139.93% |

The ranges overlap almost completely: baseline 50.57-114.66 msg/s and current
49.78-121.32 msg/s. These short Btrfs runs cannot support a stable improvement
claim; selecting only their independent medians would be misleading.

## Conclusion

This end-to-end simulation does **not** demonstrate a current bridge
forwarding throughput improvement over the clean pre-cutover implementation.
The controlled result is a 6.00% throughput regression, and the long
production-filesystem pair is an 11.47% regression. Tail latency is
approximately unchanged.

The result matches the real database architecture. `DbManager` already owns
one SQLite writer thread. The old actor worker synchronously waited for that
writer; the new implementation submits to `BlockingExecutor`, whose worker
then waits for the same writer. This releases actor workers for unrelated
mailboxes but cannot increase the single writer's capacity, and it adds
executor/completion hops to each successful bridge route.

A separate co-tenant benchmark is needed to quantify the intended benefit:
latency of unrelated actor mailboxes while bridge SQLite work is held. It
should not be reported as bridge forwarding throughput. Improving the bridge
throughput itself requires reducing database/executor round trips or replacing
the blocking wait on `DbManager`'s writer with an event-driven database
completion boundary.

## Reproduction

Build the current benchmark and actors:

```bash
cmake -S . -B build/actor-dev \
  -DCMAKE_BUILD_TYPE=Debug \
  -DOBCX_BUILD_BENCHMARKS=ON
cmake --build build/actor-dev \
  --target bridge_forwarding_business_benchmark \
           message_store_actor bridge_actor -j2
```

Run against an isolated Btrfs sandbox:

```bash
build/actor-dev/benchmarks/bridge_forwarding_business_benchmark \
  build/actor-dev/actors/message_store.so \
  build/actor-dev/actors/bridge.so \
  --label candidate \
  --messages 10000 \
  --concurrency 256 \
  --warmup 512 \
  --groups 8 \
  --users 64 \
  --reply-every 5 \
  --expect-post-send-recovery-reads 0 \
  --expect-mapping-upserts 10520 \
  --sandbox-parent "$PWD/build/benchmark-sandboxes"
```
