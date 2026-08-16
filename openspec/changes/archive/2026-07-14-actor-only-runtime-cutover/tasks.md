## 1. Freeze The Cutover Contract And Baselines

- [x] 1.1 Record the exact actor-only public API, configuration, installed SDK, package metadata, registry schema, and repository naming inventory
- [x] 1.2 Reproduce the archived V1 and native-v2 benchmarks with fixed commands, workloads, and hardware allocation
- [x] 1.3 Convert correctness and performance thresholds into repeatable tests

## 2. Qualify The Native Scheduler

- [x] 2.1 Profile the balanced-load regression using scheduler queue, wake, steal, CPU, allocation, and latency metrics
- [x] 2.2 Fix the native scheduler bottleneck without weakening mailbox exclusivity, backpressure, or exactly-once transitions
- [x] 2.3 Pass balanced, skewed, I/O-heavy, completion-storm, shutdown, CPU, and memory thresholds on the designated environment
- [x] 2.4 Complete one million seeded enqueue, resume, suspend, completion, cancellation, and shutdown transitions without loss, duplication, hang, or post-destruction resume
- [x] 2.5 Pass ActorTask, work-stealing, mailbox, and Asio-interoperability suites under ASan/UBSan and TSan

## 3. Establish The Actor Package Ecosystem

- [x] 3.1 Define canonical `actor.toml` identity, ABI, artifact, dependency, and publication fields with validation tests
- [x] 3.2 Change dependency and packaging tools to read only canonical actor metadata
- [x] 3.3 Reduce the installed SDK to V2 actor headers, libraries, export helpers, and `OBCXActor.cmake`
- [x] 3.4 Add a clean-install external actor test covering configure, compile, link, install, load, invoke, and unload
- [x] 3.5 Rename the local extension checkout and build paths from `local_plugin` to `local_actor`
- [x] 3.6 Update CI, packaging, vcpkg generation, examples, and build presets for actor package paths and metadata
- [x] 3.7 Replace the official plugin template with a clean-install V2 actor package template and retire remaining plugin-only entries from the core build inventory

## 4. Replace The Plugin Registry

- [x] 4.1 Define the actor registry entry and generated index schemas for V2 actor packages
- [x] 4.2 Rename registry source, distribution paths, documentation, and automation from plugin to actor terminology
- [x] 4.3 Update registry generation and submission validation to accept actor package fields only
- [x] 4.4 Publish and validate actor entries for bridge and message-store
- [x] 4.5 Add registry tests for valid publication, invalid actor metadata, deterministic index generation, and artifact resolution

## 5. Cut Over Standalone Actors

- [x] 5.1 Remove QQ-to-TG and TG-to-QQ plugin wrapper targets and exports from the bridge repository
- [x] 5.2 Make the V2 bridge actor the only bridge entry point while retaining QQ and Telegram I/O behind `ActorContext::await_asio`
- [x] 5.3 Verify bridge forwarding, mapping, retry, media, failure, suspension, and shutdown behavior through actor pipeline tests
- [ ] 5.4 Rename bridge repository/package metadata and active documentation that still advertise a plugin
  - Local checkout, package metadata, publication URL, and active docs are actor-named. The upstream source is still `vollate/obcx-plugin-bridge`, while `vollate/obcx-actor-bridge` does not yet exist; the repository rename remains a coordinated external release action.
- [x] 5.5 Change message-store to canonical actor metadata and V2 actor SDK/CMake surfaces only
- [x] 5.6 Pass clean installed-SDK builds and repository smoke tests for bridge and message-store
- [x] 5.7 Test core, bridge, message-store, and actor-registry together through the cross-repository suite

## 6. Remove The Legacy Plugin Model From Core

- [x] 6.1 Remove `IPlugin`, plugin export macros, plugin implementation sources, and plugin public-header installation
- [x] 6.2 Remove `PluginManager`, `SafePluginWrapper`, plugin loading, initialization, shutdown, and runtime construction
- [x] 6.3 Remove plugin CLI reload behavior and component-manager dependencies on PluginManager
- [x] 6.4 Remove plugin configuration structs, accessors, and `[plugins]` parsing without adding legacy-input detection or warnings
- [x] 6.5 Remove plugin manifests, plugin dependency extraction, `OBCXPlugin.cmake`, plugin targets, fixtures, and topology tests
- [x] 6.6 Remove plugin libraries and headers from exported CMake targets, installed packages, and distribution artifacts

## 7. Remove V1 Actors And Asio-v1

- [x] 7.1 Remove V1 `IActor`, V1 ABI factories, V1 exports, and V1 actor fixtures
- [x] 7.2 Simplify ActorManager to recognize, construct, retain, and destroy the supported V2 ABI only
- [x] 7.3 Remove `AsioActorV1Adapter`, mixed-version scheduler registration, and mixed-pipeline tests
- [x] 7.4 Remove `allow_v1_actors` and all V1 compatibility state from configuration, runtime options, and public APIs
- [x] 7.5 Remove the Asio-v1 fixed-shard scheduler implementation, target, tests, benchmarks, and rollback probe
- [x] 7.6 Remove actor engine selection and construct the native scheduler directly in the runtime bundle
- [x] 7.7 Update orchestrator and runtime tests to cover only V2 ActorTask dispatch through the native scheduler

## 8. Audit Active Surfaces And Documentation

- [x] 8.1 Add a source and installed-artifact audit for retired headers, symbols, CMake targets, config APIs, loader branches, and runtime engines
- [x] 8.2 Update README, current architecture ADRs, actor author guide, operations guide, examples, and configuration reference for actor-only operation
- [x] 8.3 Regenerate English and Chinese API documentation without active plugin, V1 actor, or Asio-v1 pages
- [x] 8.4 Publish a breaking-change notice that documents the supported actor setup without promising handling for retired inputs or binaries
- [x] 8.5 Confirm historical records are clearly marked and cannot be mistaken for current extension documentation

## 9. Final Conformance And Coordinated Release

- [x] 9.1 Pass all core unit, integration, deterministic race, stress, sanitizer, benchmark, and shutdown gates after removal
- [x] 9.2 Pass the bridge, message-store, and actor-registry build, publication, load, and end-to-end smoke suite
  - A fresh RelWithDebInfo conformance build passed installed-artifact dynamic loading and the real message-store-to-bridge pipeline.
- [x] 9.3 Verify a clean machine can configure, build, install, start, load actors, process a representative pipeline, and shut down cleanly
  - An empty `/tmp` build/install root under `nix develop --ignore-environment` completed a Release configure/build, installed one relocatable deployment, started installed `obcx`, dynamically loaded installed bridge/message-store actors, persisted and forwarded the representative pipeline, and shut down cleanly.
- [x] 9.4 Run the release soak and rehearse deployment rollback to the preceding OBCX release without adding a fallback to the actor-only binary
  - One installed runtime processed 100,000 messages with zero failures. An atomic deployment-link rollback then switched from the healthy candidate to the immutable direct pre-cutover `origin/main` baseline; the previous binary passed version/CLI health and remained selected.
- [ ] 9.5 Publish coordinated core, bridge, message-store, and actor-registry artifacts only after every exit gate is recorded green
  - `scripts/package_actor_release.py` reproducibly prepares the core, bridge, message-store, and actor-registry archives, raw actor assets, manifest, and checksums. The manifest is deliberately `prepared-not-published`.
  - Actual tags and uploads remain external repository actions and are not claimed by local artifact preparation; publication must follow the upstream bridge/template/registry renames.
