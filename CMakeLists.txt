cmake_minimum_required(VERSION 3.20)

if(NOT CMAKE_CXX_COMPILER)
  find_program(CLANG_CXX_COMPILER clang++)
  find_program(GNU_CXX_COMPILER g++)

  if(CLANG_CXX_COMPILER)
    set(CMAKE_CXX_COMPILER ${CLANG_CXX_COMPILER})
    message(STATUS "Using LLVM Clang++ compiler: ${CLANG_CXX_COMPILER}")
  elseif(GNU_CXX_COMPILER)
    set(CMAKE_CXX_COMPILER ${GNU_CXX_COMPILER})
    message(STATUS "Using GNU G++ compiler: ${GNU_CXX_COMPILER}")
  endif()
endif()

if(NOT CMAKE_C_COMPILER)
  find_program(CLANG_C_COMPILER clang)
  find_program(GNU_C_COMPILER gcc)

  if(CLANG_C_COMPILER)
    set(CMAKE_C_COMPILER ${CLANG_C_COMPILER})
    message(STATUS "Using LLVM Clang compiler: ${CLANG_C_COMPILER}")
  elseif(GNU_C_COMPILER)
    set(CMAKE_C_COMPILER ${GNU_C_COMPILER})
    message(STATUS "Using GNU GCC compiler: ${GNU_C_COMPILER}")
  endif()
endif()

# On macOS, prefer CommandLineTools SDK over Xcode SDK to avoid header search
# path conflicts when using Homebrew LLVM (its clang config points to
# CommandLineTools, but CMake's xcrun picks Xcode SDK and inserts it first).
#
# Inside a Nix dev shell this override is actively harmful: the nix-supplied
# clang wrapper has its own pinned apple-sdk baked into its config, and any
# `-isysroot` CMake injects would point at the runner's CommandLineTools SDK,
# which generally does not match the libc++ version pinned by nix. We
# therefore skip the override and explicitly clear CMAKE_OSX_SYSROOT so
# CMake's xcrun auto-detection does NOT pass -isysroot at all — the wrapper
# then supplies the correct sysroot itself.
if(APPLE AND NOT CMAKE_OSX_SYSROOT)
  if("$ENV{IN_NIX_SHELL}" STREQUAL "")
    execute_process(
      COMMAND xcrun --sdk macosx --show-sdk-path
      OUTPUT_VARIABLE XCRUN_SDK_PATH
      OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    set(_CLT_SDK
        "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk")
    if(EXISTS "${_CLT_SDK}" AND NOT "${XCRUN_SDK_PATH}" MATCHES
                                 "CommandLineTools")
      set(CMAKE_OSX_SYSROOT
          "${_CLT_SDK}"
          CACHE PATH "Use CommandLineTools SDK to match Homebrew LLVM sysroot"
                FORCE)
      message(
        STATUS
          "Overriding Xcode SDK with CommandLineTools SDK: ${CMAKE_OSX_SYSROOT}")
    endif()
    unset(_CLT_SDK)
  else()
    set(CMAKE_OSX_SYSROOT "" CACHE STRING
        "Cleared in IN_NIX_SHELL so the nix clang wrapper supplies its own pinned apple-sdk"
        FORCE)
    message(STATUS "Nix dev shell detected: CMAKE_OSX_SYSROOT cleared")
  endif()
endif()

project(OBCX VERSION 1.1.0 LANGUAGES CXX)

set(OBCX_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(OBCX_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(OBCX_VERSION_PATCH ${PROJECT_VERSION_PATCH})

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.hpp.in"
  "${CMAKE_BINARY_DIR}/generated/obcx/version.hpp"
  @ONLY)

if(WIN32)
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(VCPKG_TRIPLET "x64-windows")
  else()
    set(VCPKG_TRIPLET "x86-windows")
  endif()
elseif(APPLE)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
    set(VCPKG_TRIPLET "arm64-osx")
  else()
    set(VCPKG_TRIPLET "x64-osx")
  endif()
elseif(UNIX)
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(VCPKG_TRIPLET "x64-linux")
  else()
    set(VCPKG_TRIPLET "x86-linux")
  endif()
endif()

if("$ENV{IN_NIX_SHELL}" STREQUAL "")
  set(CMAKE_PREFIX_PATH
      "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/${VCPKG_TRIPLET}")
else()
  message(STATUS "NIX shell activation detected, disable vcpkg")
endif()

option(ENABLE_DEBUG_TRACE "Enable logging with file and line numbers" OFF)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
  set(CMAKE_UNITY_BUILD ON)
  set(CMAKE_UNITY_BUILD_BATCH_SIZE 10)
endif()

if(ENABLE_DEBUG_TRACE)
  message(
    STATUS
      "Debug trace logging is enabled. Logs will include file and line numbers."
  )
  add_compile_definitions(OBCX_DEBUG_TRACE)
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_definitions(__OBCX_DEBUG_COMPILATION)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
  # For homebrew clang
  if(APPLE)
    set(CMAKE_THREAD_LIBS_INIT "-lpthread")
    set(CMAKE_HAVE_THREADS_LIBRARY 1)
    set(CMAKE_USE_WIN32_THREADS_INIT 0)
    set(CMAKE_USE_PTHREADS_INIT 1)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
  endif()
  set(CMAKE_LINKER_TYPE LLD)
  message(STATUS "Using libc++ standard library with Clang")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(
  Boost CONFIG REQUIRED
  COMPONENTS thread
  OPTIONAL_COMPONENTS system)
if(TARGET Boost::system)
  set(BOOST_SYSTEM_TARGET Boost::system)
else()
  set(BOOST_SYSTEM_TARGET Boost::headers)
endif()
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(unofficial-brotli CONFIG)
if(unofficial-brotli_FOUND)
  set(BROTLIDEC_TARGET unofficial::brotli::brotlidec)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(BROTLIDEC REQUIRED IMPORTED_TARGET libbrotlidec)
  set(BROTLIDEC_TARGET PkgConfig::BROTLIDEC)
endif()

find_package(zstd CONFIG)
if(zstd_FOUND)
  if(TARGET zstd::libzstd_static)
    set(ZSTD_TARGET zstd::libzstd_static)
  else()
    set(ZSTD_TARGET zstd::libzstd_shared)
  endif()
else()
  if(NOT PkgConfig_FOUND)
    find_package(PkgConfig REQUIRED)
  endif()
  pkg_check_modules(LIBZSTD REQUIRED IMPORTED_TARGET libzstd)
  set(ZSTD_TARGET PkgConfig::LIBZSTD)
endif()

find_package(tomlplusplus CONFIG REQUIRED)
find_package(ftxui CONFIG REQUIRED)
find_package(re2 CONFIG REQUIRED)

# ============================================================================
# io_uring support (Linux only)
# ----------------------------------------------------------------------------
# Boost.Asio can use io_uring as its reactor backend (kernel >= 5.10, liburing
# available). This replaces the epoll-based implementation for socket I/O and
# is significantly faster for high-concurrency async workloads.
#
# We enable it automatically on Linux when liburing is found. Set
# OBCX_ENABLE_IO_URING=OFF to opt out, or OBCX_ENABLE_IO_URING=ON to require
# it (fails the configure step if liburing is missing).
# ============================================================================
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  option(OBCX_ENABLE_IO_URING
         "Use io_uring as Boost.Asio's reactor backend (requires liburing)"
         ON)
else()
  set(OBCX_ENABLE_IO_URING OFF CACHE BOOL
      "io_uring is Linux-only" FORCE)
endif()

set(OBCX_IO_URING_TARGET "")
if(OBCX_ENABLE_IO_URING)
  if(NOT PkgConfig_FOUND)
    find_package(PkgConfig REQUIRED)
  endif()
  pkg_check_modules(LIBURING IMPORTED_TARGET liburing)
  if(LIBURING_FOUND)
    set(OBCX_IO_URING_TARGET PkgConfig::LIBURING)
    message(STATUS "io_uring backend enabled for Boost.Asio "
                   "(liburing ${LIBURING_VERSION})")
  else()
    message(WARNING
      "OBCX_ENABLE_IO_URING=ON but liburing was not found via pkg-config; "
      "falling back to the default epoll reactor. Install liburing-dev "
      "(Debian/Ubuntu) or liburing (Arch/Fedora) to enable io_uring.")
    set(OBCX_ENABLE_IO_URING OFF)
  endif()
endif()

include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_BINARY_DIR}/generated)

option(OBCX_BUILD_TESTS "Build tests" OFF)

if(OBCX_BUILD_TESTS)
  enable_testing()
endif()


add_subdirectory(src)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(cmake/OBCXPluginLoader.cmake)

if(EXISTS ${CMAKE_SOURCE_DIR}/plugins.toml)
    obcx_load_plugins(${CMAKE_SOURCE_DIR}/plugins.toml)
endif()

if(OBCX_BUILD_TESTS)
  add_subdirectory(tests)
endif()
