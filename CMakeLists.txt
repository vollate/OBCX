cmake_minimum_required(VERSION 3.20)

set(OBCX_IN_NIX_SHELL OFF)
if(NOT "$ENV{IN_NIX_SHELL}" STREQUAL "")
  set(OBCX_IN_NIX_SHELL ON)
endif()

if(OBCX_IN_NIX_SHELL)
  if(APPLE)
    set(CMAKE_OSX_SYSROOT "" CACHE STRING
        "Cleared in IN_NIX_SHELL so the nix clang wrapper supplies its own pinned apple-sdk"
        FORCE)
    message(STATUS "Nix dev shell detected: CMAKE_OSX_SYSROOT cleared")

    foreach(_LINKER_FLAG_VAR
            CMAKE_EXE_LINKER_FLAGS
            CMAKE_SHARED_LINKER_FLAGS
            CMAKE_MODULE_LINKER_FLAGS)
      if("${${_LINKER_FLAG_VAR}}" MATCHES "/opt/homebrew/opt/llvm")
        string(REGEX REPLACE
               "(^| )-L/opt/homebrew/opt/llvm/lib(/unwind)?( |$)"
               " "
               _CLEANED_LINKER_FLAGS
               "${${_LINKER_FLAG_VAR}}")
        string(REGEX REPLACE "(^| )-lunwind( |$)" " " _CLEANED_LINKER_FLAGS
                             "${_CLEANED_LINKER_FLAGS}")
        string(STRIP "${_CLEANED_LINKER_FLAGS}" _CLEANED_LINKER_FLAGS)
        set(${_LINKER_FLAG_VAR}
            "${_CLEANED_LINKER_FLAGS}"
            CACHE STRING "Linker flags sanitized for Nix dev shell" FORCE)
        message(STATUS "Removed Homebrew LLVM linker flags from ${_LINKER_FLAG_VAR}")
      endif()
    endforeach()
    unset(_CLEANED_LINKER_FLAGS)
    unset(_LINKER_FLAG_VAR)
  endif()
else()
  if(APPLE AND NOT CMAKE_OSX_SYSROOT)
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

if(OBCX_IN_NIX_SHELL)
  set(_VCPKG_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/${VCPKG_TRIPLET}")
  if(_VCPKG_PREFIX IN_LIST CMAKE_PREFIX_PATH)
    list(REMOVE_ITEM CMAKE_PREFIX_PATH "${_VCPKG_PREFIX}")
    set(CMAKE_PREFIX_PATH
        "${CMAKE_PREFIX_PATH}"
        CACHE STRING "Package search prefixes" FORCE)
    message(STATUS "Removed vcpkg prefix from CMAKE_PREFIX_PATH in Nix shell")
  endif()
  unset(_VCPKG_PREFIX)
  message(STATUS "NIX shell activation detected, disable vcpkg")
else()
  set(CMAKE_PREFIX_PATH
      "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/${VCPKG_TRIPLET}")
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
  if(NOT APPLE)
    set(CMAKE_LINKER_TYPE LLD)
  endif()
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
