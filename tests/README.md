# 测试目录

`tests/` 只保存 OBCX 根仓库拥有的可重复自动化测试。QQ、LLOneBot、Docker
Compose 和包含凭据的本地配置位于 `dev/onebot/`，不属于测试门禁。

## 所有权边界

根仓库测试可以覆盖：

- actor runtime、scheduler、Asio/BlockingExecutor、reload 与通用 V2 ABI；
- 根仓库网络、OneBot/Telegram adapter、CLI 与数据库组件；
- metadata、registry、packaging、安装后 SDK 与通用 fixture actor。

根测试不得包含生产 actor 的私有头文件、实现源码或业务断言。对应测试归属如下：

| 行为 | 所有者 |
| --- | --- |
| Bridge 转发、mapping、媒体、重试、真实 Message Store → Bridge pipeline、bot-facing reload | `local_actor/obcx-actor-bridge/tests/` |
| Message Store schema、持久化、identity、deduplication、MessageStored emission | `local_actor/obcx-actor-message-store/tests/` |
| 通用 ABI、same-SONAME staging、dependency isolation、generation cutover | 根 `tests/fixtures/` 与 `tests/cpp/` |
| 跨仓库安装与构建协调 | 根 conformance CMake 脚本；actor 仓库拥有业务测试源码和 CTest 注册 |

本次迁移清单：

- 根 `standalone_actor_pipeline_smoke.cpp` 与
  `standalone_actor_reload_smoke.cpp` 已迁至 Bridge tests；
- real Message Store/Bridge unmatched-slash regression 已由 Bridge installed
  reload smoke 覆盖；
- root reload 的 rebuilt Message Store fixture 已替换为通用 rebuilt actor；
- Bridge/Message Store 嵌入根构建时只生成 DSO，不注册 actor-owned tests。

Bridge 使用 `OBCX_BRIDGE_BUILD_TESTS`，Message Store 使用
`OBCX_MESSAGE_STORE_BUILD_TESTS`。两者在 standalone top-level build 中跟随
`BUILD_TESTING` 默认开启，作为子目录嵌入时默认关闭；特殊 consumer 可显式覆盖。

## 目录职责

- `cpp/`：GoogleTest 单元与小型集成测试。
- `python/`：Python `unittest` metadata、packaging 与架构约束。
- `cmake/`：由 CTest 调用的 SDK、CLI、inventory 与跨仓库协调脚本。
- `compile/`：C++ 正向与负向反射编译契约。
- `fixtures/`：根 runtime 专用的通用 actor DSO 与 standalone SDK consumer。
- `support/`：多个根测试共享的辅助代码。

`CMakeLists.txt` 只负责引入注册模块：

- `cmake/reflection_compile_tests.cmake`
- `cmake/actor_fixtures.cmake`
- `cmake/unit_tests.cmake`
- `cmake/python_tests.cmake`
- `cmake/integration_tests.cmake`

## 测试层级

快速根测试，不执行 compile/package/conformance 门禁：

```bash
cmake --preset actor-dev
cmake --build --preset actor-dev --parallel
ctest --preset actor-fast
```

完整根测试，包括反射编译、Python 架构/package、CLI 与 installed-SDK：

```bash
ctest --preset actor-full
```

干净安装 SDK 后构建并测试各 standalone actor 与 registry：

```bash
cmake --preset actor-conformance
cmake --build --preset actor-conformance --parallel
ctest --preset actor-conformance
```

标签仍可用于进一步缩小范围：

```bash
ctest --preset actor-dev -L actor-runtime
ctest --preset actor-dev -L network
ctest --preset actor-conformance -L conformance
```

## 确定性 WebSocket 测试

WebSocket FIFO、bounded backpressure、write failure、shutdown、OneBot echo
response/timeout race 使用手动 write gate 与 deadline 驱动，并默认进入 fast/full
门禁。测试不得用固定 `sleep_for` 或真实响应时长证明正确性；`wait_for` 只可作为
发现 deadlock 的有界 watchdog。Beast loopback smoke 使用 listening、connected、message
completion signal，不使用 startup sleep。

Python 测试也可以直接运行，例如：

```bash
python3 -m unittest -v tests/python/actor_metadata_test.py
```

新增 C++ 测试时，使用 `cmake/unit_tests.cmake` 中的 `obcx_add_gtest`
注册 target 与职责标签。Python 产生的 `__pycache__`、本地 bot 环境和 build
outputs 必须保持 ignored，不属于测试源码。
