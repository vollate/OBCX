add_library(
        obcx_core SHARED
        common/logger.cpp
        common/cli_handler.cpp
        tui/tui_app.cpp
        tui/tui_layout.cpp
        common/json_utils.cpp
        common/message_type.cpp
        common/config_loader.cpp
        network/compression.cpp
        network/http_client.cpp
        network/http_client_sync.cpp
        network/websocket_client.cpp
        network/websocket_write_queue.cpp
        network/proxy_http_client.cpp
        network/proxy_tunnel.cpp
        onebot11/network/http/connection_manager.cpp
        onebot11/network/websocket/action_request_tracker.cpp
        onebot11/network/websocket/connection_manager.cpp
        onebot11/adapter/protocol_adapter.cpp
        onebot11/adapter/message_converter.cpp
        onebot11/adapter/event_converter.cpp
        telegram/adapter/protocol_adapter.cpp
        telegram/network/http/connection_manager.cpp
        core/actor/actor_manager.cpp
        core/actor/actor_package_stager.cpp
        core/actor/actor_work_stealing_executor.cpp
        core/actor/native_actor_scheduler.cpp
        core/bot/bot_command_catalog_component.cpp
        core/bot/bot_component_runtime.cpp
        core/bot/bot_event_components.cpp
        core/bot/bot_installation_assembler.cpp
        core/bot/bot_installation_directory.cpp
        core/bot/bot_operation_components.cpp
        core/bot/bot_operation_dispatcher.cpp
        core/bot/bot_operation_response_parser.cpp
        core/bot/bot_protocol_components.cpp
        core/bot/bot_transport_components.cpp
        core/command/command_coordinator.cpp
        core/command/command_matcher.cpp
        core/command/command_platform_adapter.cpp
        core/infrastructure/db_manager.cpp
        core/infrastructure/process_staging_uuid.cpp
        core/runtime/actor_runtime_reload_controller.cpp
        core/runtime/message_event_ingress.cpp
        core/runtime/orchestrator.cpp
        core/runtime/runtime_generation.cpp
        core/runtime/runtime_thread_budget.cpp)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

target_include_directories(obcx_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/obcx>)

target_link_libraries(
        obcx_core
        PUBLIC fmt::fmt
        spdlog::spdlog
        ${BOOST_SYSTEM_TARGET}
        ZLIB::ZLIB
        ${BROTLIDEC_TARGET}
        ${ZSTD_TARGET}
        nlohmann_json::nlohmann_json
        Threads::Threads
        OpenSSL::SSL
        OpenSSL::Crypto
        ${OBCX_SQLITE_TARGET}
        tomlplusplus::tomlplusplus
        ftxui::screen
        ftxui::dom
        ftxui::component
        re2::re2)

target_compile_features(obcx_core PUBLIC cxx_std_26)
target_compile_options(obcx_core PUBLIC -freflection)
target_compile_definitions(obcx_core PRIVATE
    OBCX_PATCHELF_EXECUTABLE="${OBCX_PATCHELF_EXECUTABLE}")

# ============================================================================
# Boost.Asio io_uring backend
# ----------------------------------------------------------------------------
# When OBCX_ENABLE_IO_URING is ON and liburing was found (see top-level
# CMakeLists.txt), expose Boost.Asio's io_uring support and link liburing.
# Keep epoll enabled so socket/timer I/O does not depend on io_uring queue
# creation succeeding at runtime. These macros are PUBLIC because Asio is
# header-only and every translation unit that includes <boost/asio.hpp> must
# see the same config.
# ============================================================================
if(OBCX_ENABLE_IO_URING AND OBCX_IO_URING_TARGET)
  target_compile_definitions(obcx_core PUBLIC BOOST_ASIO_HAS_IO_URING=1)
  target_link_libraries(obcx_core PUBLIC ${OBCX_IO_URING_TARGET})
endif()

# Enable position independent code for shared library compatibility
set_target_properties(obcx_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Alias target for consistent usage (in-tree and installed)
add_library(obcx::obcx_core ALIAS obcx_core)

# Include the generated version header
target_include_directories(obcx_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>)

# ===== Install rules =====

# Install the library target
install(TARGETS obcx_core
    EXPORT obcx-sdk-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/obcx)

# Install the actor-package SDK surface. Runtime application, protocol, and
# transport implementation headers are intentionally not exported.
install(FILES
    ${CMAKE_SOURCE_DIR}/include/common/config_loader.hpp
    ${CMAKE_SOURCE_DIR}/include/common/json_utils.hpp
    ${CMAKE_SOURCE_DIR}/include/common/logger.hpp
    ${CMAKE_SOURCE_DIR}/include/common/message_type.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/obcx/common)

install(FILES
    ${CMAKE_SOURCE_DIR}/include/core/actor/actor.hpp
    ${CMAKE_SOURCE_DIR}/include/core/actor/actor_asio.hpp
    ${CMAKE_SOURCE_DIR}/include/core/actor/actor_commands.hpp
    ${CMAKE_SOURCE_DIR}/include/core/actor/actor_manager.hpp
    ${CMAKE_SOURCE_DIR}/include/core/actor/actor_messages.hpp
    ${CMAKE_SOURCE_DIR}/include/core/actor/actor_task.hpp
    ${CMAKE_SOURCE_DIR}/include/core/actor/actor_work_stealing_executor.hpp
    ${CMAKE_SOURCE_DIR}/include/core/actor/blocking_executor.hpp
    ${CMAKE_SOURCE_DIR}/include/core/actor/native_actor_scheduler.hpp
    ${CMAKE_SOURCE_DIR}/include/core/actor/reflected_actor.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/obcx/core/actor)

install(FILES
    ${CMAKE_SOURCE_DIR}/include/core/bot/bot_operation_client.hpp
    ${CMAKE_SOURCE_DIR}/include/core/bot/bot_operation_contract.hpp
    ${CMAKE_SOURCE_DIR}/include/core/bot/bot_operation_types.hpp
    ${CMAKE_SOURCE_DIR}/include/core/bot/bot_operations.hpp
    ${CMAKE_SOURCE_DIR}/include/core/bot/onebot11_bot_operations.hpp
    ${CMAKE_SOURCE_DIR}/include/core/bot/telegram_bot_operations.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/obcx/core/bot)

install(FILES
    ${CMAKE_SOURCE_DIR}/include/core/infrastructure/db_manager.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/obcx/core/infrastructure)

install(FILES
    ${CMAKE_SOURCE_DIR}/include/network/http_client.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/obcx/network)

# Install the generated version.hpp
install(FILES ${CMAKE_BINARY_DIR}/generated/obcx/version.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/obcx/obcx)

# Export targets with namespace
install(EXPORT obcx-sdk-targets
    FILE obcx-sdk-targets.cmake
    NAMESPACE obcx::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/obcx-sdk)

# Generate and install package config files
configure_package_config_file(
    ${CMAKE_SOURCE_DIR}/cmake/obcx-sdk-config.cmake.in
    ${CMAKE_BINARY_DIR}/obcx-sdk-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/obcx-sdk)

write_basic_package_version_file(
    ${CMAKE_BINARY_DIR}/obcx-sdk-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

install(FILES
    ${CMAKE_BINARY_DIR}/obcx-sdk-config.cmake
    ${CMAKE_BINARY_DIR}/obcx-sdk-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/obcx-sdk)

# Install build helper modules alongside the SDK
install(FILES
    ${CMAKE_SOURCE_DIR}/cmake/OBCXActor.cmake
    ${CMAKE_SOURCE_DIR}/cmake/actor_metadata.py
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/obcx-sdk)

add_subdirectory(app)
