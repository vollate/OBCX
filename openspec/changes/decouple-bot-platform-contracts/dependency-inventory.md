# Implementation baseline / dependency inventory

## Revisions and boundaries

Observed at implementation start (the root's actual revision is
`fb80ac6`, not the earlier conversation's `55a811e`):

| Repository | Revision | Migration responsibility |
| --- | --- | --- |
| core | `fb80ac6` | SDK, runtime, configuration, actor loader, exports and tests |
| `local_actor/obcx-actor-bridge` | `8f0a9c1` | Both platform adapters, fake gateway, retry, metadata |
| `local_actor/chat_llm` | `3412e48` | Common + Telegram adapters, fake gateway, metadata |
| `local_actor/obcx-actor-message-store` | `1be9ce6` | Rebuild generated input contract even without Bot egress |
| `local_actor/obcx-actor-template` | `74d2085` | Rebuild helper/SDK smoke, metadata |
| `local_actor/obcx-actor-registry` | `69457df` | Entries, generated index and validation tests |

`legacy-dependencies.txt` is a sorted 90-file textual inventory of enum,
all-platform-client, connection-variant or schema-1 references in root and
nested repositories. It includes the new golden test as a consumer to migrate.
It is a starting inventory, not a claim that all text matches denote the same
schema or that transitive include dependencies are captured by grep.

Search expression:

```text
BotSurface|BotAction|action_ids::all|action_supports_surface|BotOperationClient|
BotInstallationSurface|BotConnectionConfig|schema_version.{0,8}1
```

Excluded generated/build trees, bundles and historical research are not runtime
consumers. Header/include closure, installed SDK and isolated runtime builds
remain the acceptance tests. Public `common/message_type.hpp` currently depends
only on common JSON and standard headers: preserving its existing MessageSegment
shape does not require including a provider SDK.

## Root ownership hot spots

- `include/core/bot/bot_operation_types.hpp`: enums, ordinal lookup, global
  compatibility matrix, shared references/errors and Telegram topic target.
- `bot_operations.hpp`, `onebot11_bot_operations.hpp`,
  `telegram_bot_operations.hpp`, `bot_operation_contract.hpp`,
  `bot_operation_client.hpp`: all-platform DTO/include/virtual dependency.
- `src/core/bot/bot_operation_{dispatcher,components}.cpp`: routes, response
  parsers, per-action overrides, supported sets; split without altering safety.
- `include/common/config_loader.hpp`, `src/common/config_loader.cpp`: private
  credential configs, central `BotConnectionConfig` variant, full raw snapshot.
- `bot_installation_assembler`, `bot_component_runtime`, event/protocol/transport
  components and installation directory: recipes and concrete platform types.
- `src/core/command/`, `src/core/runtime/runtime_generation.cpp`, `src/app/main.cpp`:
  Telegram config/catalog plumbing, surface-to-ingress mapping, generation
  references, private fingerprint inputs. Include non-enum platform dependencies
  when splitting these modules.
- `src/CMakeLists.txt`, `cmake/obcx-sdk-config.cmake.in`, root SDK version metadata:
  currently a combined `obcx_core` target and explicit install header list.

## Actor compatibility fixtures

`tests/fixtures/frozen_schema1_actor.cpp` is an independent DSO with literal old
ABI-2/schema-1 metadata and no OBCX include/link dependency. Its factory and
preparation exports count calls; the factory never manufactures an incompatible
C++ actor object. Baseline ActorManager discovery accepts its metadata without
calling either export. Task 8.2 must invert this to reject schema 1 before both
exports during startup/validation/reload and preserve counters through `dlclose`
by retaining the test's handle. This fixture proves the metadata gate, not
cross-toolchain object compatibility.

`tests/fixtures/legacy_v2_actor.cpp` serves a different purpose: a compatible
actor without the optional preparation export. It must migrate to schema 2,
not be conflated with the frozen old SDK fixture.

Also migrate `reflected_actor.hpp`, literal contract fixtures (including the
currently unsupported schema-2 case), activation/reload fixtures, installed
standalone smoke, and generated actor input metadata checks.

**Do not blindly replace `schema_version = 1`:** `actor.toml`, registry index
and release manifest schemas are package formats, distinct from
`obcx_get_actor_contract()` input schema. Keep those formats unless their own
schema changes are required; update SDK constraints and rebuilt metadata.
Bridge database schema 3 and Message Store state remain unchanged.

## Offline inputs (not local actor HEADs)

Bundle heads verified with `git bundle list-heads`:

| Actor | Archived base | Patched restore output |
| --- | --- | --- |
| bridge | `de8c3046c218c9e2a254abe832e91595f4cc629a` | same |
| message-store | `3a9dfc2b27375d22531b4308356b75f4bac7077f` | `d3511ae5950a0e6454458eb763b9947165397d2a` |
| registry | `057b46522872bfbc2dd87435e3751b8d2001e26b` | `ff8a4fecdabd91b2b5e930c39454389bb72109eb` |
| template | `993048e6d7e280167cc2189a51464d8fd9197c68` | `4bc3c5558a6864d9a067c486a978f943b90cb1f6` |

Update `packaging/actors/bundles/`, `patches/`, `restore-sources.sh`,
`apply-patches.sh`, `README.md` together; the scripts verify exact revisions.
Current bundled actors exclude Chat LLM. Its standalone suite still must run.
Do not equate local actor HEADs with restored sources or blindly substitute pins.
Other consumers: actor registry entries/index, `cmake/parse_actor_packages.py`,
release packaging/verification/rollback scripts, `tests/cmake/run_v2_sdk_smoke.cmake`,
`run_standalone_actor_v2_repositories.cmake`, Python architecture/inventory checks,
benchmarks and deployment docs.

## Baseline verification

- Main specifications: 24/24 pass strict OpenSpec validation after ordered sync.
- Configuration tests: 7/7 pass, including enabled/disabled mandatory omissions.
- Contract/golden/component tests: 30/30 pass, covering all 13 golden actions.
- ActorManager tests: 13/13 pass with the frozen metadata fixture.
- No runtime/SDK migration, provider calls, database changes or commits in phase 1.
