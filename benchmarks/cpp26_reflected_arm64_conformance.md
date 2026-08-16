# C++26 Reflected Actor ARM64 Conformance

Recorded: 2026-07-15

## Supported toolchain

- Target: `aarch64-linux`
- Compiler: GCC 16.1.0
- Language mode: C++26 with `-freflection`
- Admission result: `OBCX_CXX26_REFLECTION_SUPPORTED=1`
- CMake target processor: `aarch64`
- Nix shell derivation: `/nix/store/6z99fpd9qv8d2kxnvrns501inkwvir5j-nix-shell.drv`

The flake's native ARM64 development closure was realized on the x86_64
verification host. Nix correctly declined to execute the native shell
derivation without an ARM builder. As a supplemental local result, the
realized ARM GCC, assembler, linker, and dependency closure were executed
through `qemu-aarch64`. The release gate in `.github/workflows/ci.yml` remains
the authoritative native run on `ubuntu-24.04-arm`.

## Compile conformance

The ARM CMake configure completed the hard compiler/reflection admission probe
and all 14 reflected handler `try_compile` cases. This covered the valid
sync/async overload set and every required negative diagnostic anchor:
no handler, visibility, arity, message cv/ref, envelope, context, return type,
duplicate input, missing decode/encode, and unstable/local identities.

The complete configured build produced ARM64 `libobcx_core.so`, `obcx`, the
reflected actor fixtures, and all configured C++ test executables.

| Artifact | SHA-256 |
| --- | --- |
| `src/libobcx_core.so` | `a6171230be886ed2d93c47b3e74e386d363ab7ae372dc73409b0199eaa3be82e` |
| `src/app/obcx` | `46076506c7ecda0ab9279f2d2e15aae915f9bf767527f60b55bdca1ecae7289d` |
| `tests/reflected_actor_test` | `ada9c84be07d807f52cb3cb399149529c27bfd3d18af315e13f8e871e6f88061` |

`file` identified each recorded artifact as `ELF 64-bit ... ARM aarch64`.

## Runtime conformance

The ARM binaries ran through `qemu-aarch64` using CTest's cross-compiling
emulator support. The focused suite passed 59 of 59 tests in 9.25 seconds:

- actor API and routing metadata;
- actor configuration and actor-aware contract validation;
- dynamic-library input-contract loading and rejection cases;
- orchestration, fan-out isolation, cycle detection, and hop limits;
- canonical reflected identity, generated contract, ADL JSON dispatch,
  sync normalization, failure handling, suspended-input lifetime, and typed
  emit;
- immediate Asio completion across nested reflected-task epoch propagation.

The affected Asio, native scheduler, and reflected-actor suite separately
passed 27 of 27 tests in 5.72 seconds under the same emulator.

Command:

```sh
ctest --test-dir /tmp/obcx-arm64-build --output-on-failure \
  -R '^(ActorApiTest|ActorConfigTest|ActorManagerTest|OrchestratorTest|ReflectedActorTest)\.'
```
