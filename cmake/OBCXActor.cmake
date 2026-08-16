# OBCXActor.cmake - Build and install canonical OBCX V2 actor packages.
#
# Every package must contain a valid actor.toml. Metadata artifact.name and
# artifact.target must match OUTPUT_NAME and the generated CMake target.

set(_OBCX_ACTOR_MODULE_DIR "${CMAKE_CURRENT_LIST_DIR}")

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
   NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|aarch64|arm64)$")
  message(FATAL_ERROR
    "OBCX actors support Linux x86_64 and arm64 only")
endif()
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR
   CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16.1)
  message(FATAL_ERROR
    "OBCX actors require GCC 16.1+ C++26 reflection; got "
    "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
endif()

include(CheckCXXSourceCompiles)
set(_OBCX_ACTOR_SAVED_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS}")
string(APPEND CMAKE_REQUIRED_FLAGS " -std=c++26 -freflection")
check_cxx_source_compiles([[
#include <meta>
#if !defined(__cpp_impl_reflection) || __cpp_impl_reflection < 202506L
#error OBCX actor SDK requires __cpp_impl_reflection >= 202506L
#endif
namespace obcx_actor_sdk_probe { struct Input {}; }
static_assert(std::meta::identifier_of(^^obcx_actor_sdk_probe::Input) == "Input");
int main() { return 0; }
]] OBCX_ACTOR_SDK_REFLECTION_SUPPORTED)
set(CMAKE_REQUIRED_FLAGS "${_OBCX_ACTOR_SAVED_REQUIRED_FLAGS}")
unset(_OBCX_ACTOR_SAVED_REQUIRED_FLAGS)
if(NOT OBCX_ACTOR_SDK_REFLECTION_SUPPORTED)
  message(FATAL_ERROR
    "OBCX actor SDK reflection probe failed; use GCC 16.1+ with "
    "-std=c++26 -freflection")
endif()

if(TARGET obcx_core)
  if(NOT TARGET obcx::obcx_core)
    add_library(obcx::obcx_core ALIAS obcx_core)
  endif()
else()
  find_package(obcx-sdk CONFIG REQUIRED)
endif()

function(obcx_add_actor ACTOR_NAME)
  cmake_parse_arguments(PARSE_ARGV 1 ACTOR ""
    "OUTPUT_NAME;METADATA;INSTALL_DESTINATION" "SOURCES;DEPS")

  if(NOT ACTOR_SOURCES)
    message(FATAL_ERROR
      "obcx_add_actor(${ACTOR_NAME}): SOURCES must not be empty")
  endif()

  if(ACTOR_METADATA)
    get_filename_component(_metadata "${ACTOR_METADATA}" ABSOLUTE
                           BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  else()
    set(_metadata "${CMAKE_CURRENT_SOURCE_DIR}/actor.toml")
  endif()
  if(NOT EXISTS "${_metadata}")
    message(FATAL_ERROR
      "obcx_add_actor(${ACTOR_NAME}): canonical actor.toml is required at "
      "${_metadata}")
  endif()

  find_package(Python3 3.11 COMPONENTS Interpreter REQUIRED)
  execute_process(
    COMMAND "${Python3_EXECUTABLE}"
            "${_OBCX_ACTOR_MODULE_DIR}/actor_metadata.py"
            inspect "${_metadata}" --format cmake
    OUTPUT_VARIABLE _record
    ERROR_VARIABLE _metadata_error
    RESULT_VARIABLE _metadata_result
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT _metadata_result EQUAL 0)
    message(FATAL_ERROR
      "obcx_add_actor(${ACTOR_NAME}): invalid actor metadata:\n"
      "${_metadata_error}")
  endif()
  if(NOT _record MATCHES
     "^([^|]+)\\|([^|]+)\\|([^|]+)\\|2\\|([^|]+)\\|([^|]+)$")
    message(FATAL_ERROR
      "obcx_add_actor(${ACTOR_NAME}): invalid metadata inspection record")
  endif()
  set(_actor_id "${CMAKE_MATCH_1}")
  set(_metadata_artifact "${CMAKE_MATCH_4}")
  set(_metadata_target "${CMAKE_MATCH_5}")

  set(_target "${ACTOR_NAME}_actor")
  if(ACTOR_OUTPUT_NAME)
    set(_output_name "${ACTOR_OUTPUT_NAME}")
  else()
    set(_output_name "${ACTOR_NAME}")
  endif()
  if(NOT "${_metadata_target}" STREQUAL "${_target}")
    message(FATAL_ERROR
      "obcx_add_actor(${ACTOR_NAME}): actor.toml artifact.target "
      "'${_metadata_target}' must equal '${_target}'")
  endif()
  if(NOT "${_metadata_artifact}" STREQUAL "${_output_name}")
    message(FATAL_ERROR
      "obcx_add_actor(${ACTOR_NAME}): actor.toml artifact.name "
      "'${_metadata_artifact}' must equal OUTPUT_NAME '${_output_name}'")
  endif()

  add_library(${_target} SHARED ${ACTOR_SOURCES})
  set_target_properties(${_target} PROPERTIES
    OUTPUT_NAME "${_output_name}"
    PREFIX ""
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD 26
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/actors"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/actors"
    OBCX_ACTOR_ID "${_actor_id}"
    OBCX_ACTOR_METADATA "${_metadata}")

  target_link_libraries(${_target} PRIVATE obcx::obcx_core)
  target_compile_options(${_target} PRIVATE -freflection)
  if(ACTOR_DEPS)
    target_link_libraries(${_target} PRIVATE ${ACTOR_DEPS})
  endif()
  if(TARGET obcx_core)
    target_include_directories(${_target} PRIVATE
      "${CMAKE_SOURCE_DIR}/include")
  endif()

  if(ACTOR_INSTALL_DESTINATION)
    set(_actor_install_destination "${ACTOR_INSTALL_DESTINATION}")
  else()
    set(_actor_install_destination "lib/obcx/actors")
  endif()
  install(TARGETS ${_target}
    LIBRARY DESTINATION "${_actor_install_destination}"
    RUNTIME DESTINATION "${_actor_install_destination}")
  install(FILES "${_metadata}"
    DESTINATION "share/obcx/actors/${_actor_id}"
    RENAME actor.toml)
endfunction()
