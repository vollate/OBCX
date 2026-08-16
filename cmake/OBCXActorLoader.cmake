# OBCXActorLoader.cmake - Load canonical actor packages from actors.toml.
include(FetchContent)

function(_obcx_read_actor_metadata SOURCE_DIR OUT_ID OUT_ARTIFACT OUT_TARGET)
  set(_metadata "${SOURCE_DIR}/actor.toml")
  if(NOT EXISTS "${_metadata}")
    message(FATAL_ERROR
      "[OBCX Actors] Package ${SOURCE_DIR} has no canonical actor.toml")
  endif()

  execute_process(
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/cmake/actor_metadata.py"
            inspect "${_metadata}" --format cmake
    OUTPUT_VARIABLE _record
    ERROR_VARIABLE _error
    RESULT_VARIABLE _result
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT _result EQUAL 0)
    message(FATAL_ERROR
      "[OBCX Actors] Invalid ${_metadata}:\n${_error}")
  endif()

  if(NOT _record MATCHES
     "^([^|]+)\\|([^|]+)\\|([^|]+)\\|2\\|([^|]+)\\|([^|]+)$")
    message(FATAL_ERROR
      "[OBCX Actors] Metadata inspection returned an invalid record: ${_record}")
  endif()
  set(${OUT_ID} "${CMAKE_MATCH_1}" PARENT_SCOPE)
  set(${OUT_ARTIFACT} "${CMAKE_MATCH_4}" PARENT_SCOPE)
  set(${OUT_TARGET} "${CMAKE_MATCH_5}" PARENT_SCOPE)
endfunction()

function(_obcx_add_actor_package SOURCE_DIR DEFAULT_ENABLED)
  _obcx_read_actor_metadata("${SOURCE_DIR}" _actor_id _artifact _target)
  string(MAKE_C_IDENTIFIER "${_actor_id}" _option_suffix)
  string(TOUPPER "${_option_suffix}" _option_suffix)
  set(_option "OBCX_ACTOR_${_option_suffix}")
  option(${_option} "Build actor package ${_actor_id}" ${DEFAULT_ENABLED})

  if(${_option})
    if(NOT EXISTS "${SOURCE_DIR}/CMakeLists.txt")
      message(FATAL_ERROR
        "[OBCX Actors] ${_actor_id} has no CMakeLists.txt: ${SOURCE_DIR}")
    endif()
    add_subdirectory("${SOURCE_DIR}"
                     "${CMAKE_BINARY_DIR}/actors_build/${_artifact}")
    if(NOT TARGET "${_target}")
      message(FATAL_ERROR
        "[OBCX Actors] ${_actor_id} metadata declares target ${_target}, "
        "but the package did not create it")
    endif()
    message(STATUS "[OBCX Actors] Enabled ${_actor_id} (${_artifact})")
  else()
    message(STATUS "[OBCX Actors] Disabled ${_actor_id}")
  endif()
endfunction()

function(obcx_load_actors MANIFEST_FILE)
  find_package(Python3 3.11 COMPONENTS Interpreter REQUIRED)

  execute_process(
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/cmake/parse_actor_packages.py"
            "${MANIFEST_FILE}"
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _error
    RESULT_VARIABLE _result
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT _result EQUAL 0)
    message(FATAL_ERROR
      "[OBCX Actors] Failed to parse ${MANIFEST_FILE}:\n${_error}")
  endif()

  if(_output STREQUAL "")
    message(STATUS "[OBCX Actors] No actor packages selected")
    return()
  endif()

  string(REPLACE "\n" ";" _records "${_output}")
  foreach(_record IN LISTS _records)
    if(_record MATCHES "^LOCAL\\|(true|false)\\|([^|]+)\\|$")
      set(_enabled "${CMAKE_MATCH_1}")
      set(_path "${CMAKE_MATCH_2}")
      if(IS_ABSOLUTE "${_path}")
        set(_source "${_path}")
      else()
        set(_source "${CMAKE_SOURCE_DIR}/${_path}")
      endif()
      get_filename_component(_source "${_source}" ABSOLUTE)
      if(NOT EXISTS "${_source}")
        message(FATAL_ERROR
          "[OBCX Actors] Local actor path does not exist: ${_path}")
      endif()
      _obcx_add_actor_package("${_source}" "${_enabled}")
    elseif(_record MATCHES "^REMOTE\\|(true|false)\\|([^|]+)\\|([^|]+)$")
      set(_enabled "${CMAKE_MATCH_1}")
      set(_repository "${CMAKE_MATCH_2}")
      set(_revision "${CMAKE_MATCH_3}")
      if(_enabled STREQUAL "false")
        message(STATUS "[OBCX Actors] Skipping disabled ${_repository}")
        continue()
      endif()

      string(MD5 _source_key "${_repository}@${_revision}")
      set(_fetch_name "obcx_actor_${_source_key}")
      FetchContent_Declare(${_fetch_name}
        GIT_REPOSITORY "${_repository}"
        GIT_TAG "${_revision}"
        GIT_SHALLOW FALSE
        SOURCE_DIR "${CMAKE_BINARY_DIR}/_actors/${_source_key}")
      FetchContent_GetProperties(${_fetch_name})
      if(NOT ${_fetch_name}_POPULATED)
        cmake_policy(PUSH)
        if(POLICY CMP0169)
          cmake_policy(SET CMP0169 OLD)
        endif()
        FetchContent_Populate(${_fetch_name})
        cmake_policy(POP)
      endif()
      _obcx_add_actor_package("${${_fetch_name}_SOURCE_DIR}" true)
    else()
      message(FATAL_ERROR
        "[OBCX Actors] Parser returned an invalid record: ${_record}")
    endif()
  endforeach()
endfunction()
