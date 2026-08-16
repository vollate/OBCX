if(NOT DEFINED OBCX_BUILD_DIR OR NOT DEFINED OBCX_SOURCE_DIR OR
   NOT DEFINED OBCX_CTEST_COMMAND OR NOT DEFINED OBCX_PYTHON_EXECUTABLE)
  message(FATAL_ERROR
          "OBCX build/source, CTest, and Python executable are required")
endif()

set(_conformance_root "${OBCX_BUILD_DIR}/actor-package-conformance")
set(_install_dir "${_conformance_root}/sdk")
set(_message_store_source
    "${OBCX_SOURCE_DIR}/local_actor/obcx-actor-message-store")
set(_message_store_build
    "${_conformance_root}/message-store")
set(_bridge_source "${OBCX_SOURCE_DIR}/local_actor/obcx-actor-bridge")
set(_bridge_build "${_conformance_root}/bridge")
set(_template_source "${OBCX_SOURCE_DIR}/local_actor/obcx-actor-template")
set(_template_build "${_conformance_root}/template")
set(_actor_registry_source
    "${OBCX_SOURCE_DIR}/local_actor/obcx-actor-registry")

file(REMOVE_RECURSE "${_conformance_root}")

if(NOT DEFINED OBCX_CONSUMER_BUILD_TYPE OR
   OBCX_CONSUMER_BUILD_TYPE STREQUAL "")
  set(OBCX_CONSUMER_BUILD_TYPE Release)
endif()

set(_consumer_toolchain_args)
foreach(_flag IN ITEMS C_FLAGS CXX_FLAGS EXE_LINKER_FLAGS SHARED_LINKER_FLAGS)
  if(DEFINED OBCX_CONSUMER_${_flag} AND
     NOT OBCX_CONSUMER_${_flag} STREQUAL "")
    list(APPEND _consumer_toolchain_args
         "-DCMAKE_${_flag}=${OBCX_CONSUMER_${_flag}}")
  endif()
endforeach()

set(_registry_tool
    "${OBCX_SOURCE_DIR}/actor-registry/generate_actor_index.py")
execute_process(
  COMMAND "${OBCX_PYTHON_EXECUTABLE}" -m unittest discover
          -s "${_actor_registry_source}/tests" -v
  RESULT_VARIABLE _external_registry_tests_result)
if(NOT _external_registry_tests_result EQUAL 0)
  message(FATAL_ERROR "Independent actor-registry tests failed")
endif()

set(_external_registry_tool
    "${_actor_registry_source}/generate_actor_index.py")
execute_process(
  COMMAND "${OBCX_PYTHON_EXECUTABLE}" "${_external_registry_tool}" validate
  RESULT_VARIABLE _external_registry_validate_result)
if(NOT _external_registry_validate_result EQUAL 0)
  message(FATAL_ERROR "Independent actor-registry validation failed")
endif()
execute_process(
  COMMAND "${OBCX_PYTHON_EXECUTABLE}" "${_external_registry_tool}"
          generate --check
  RESULT_VARIABLE _external_registry_index_result)
if(NOT _external_registry_index_result EQUAL 0)
  message(FATAL_ERROR "Independent actor-registry index is stale")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files
          "${OBCX_SOURCE_DIR}/cmake/actor_metadata.py"
          "${_actor_registry_source}/scripts/actor_metadata.py"
  RESULT_VARIABLE _external_registry_validator_result)
if(NOT _external_registry_validator_result EQUAL 0)
  message(FATAL_ERROR
          "Independent actor-registry canonical validator has drifted")
endif()

execute_process(
  COMMAND "${OBCX_PYTHON_EXECUTABLE}" "${_registry_tool}" validate
  RESULT_VARIABLE _registry_validate_result)
if(NOT _registry_validate_result EQUAL 0)
  message(FATAL_ERROR "Actor registry publication validation failed")
endif()

execute_process(
  COMMAND "${OBCX_PYTHON_EXECUTABLE}" "${_registry_tool}" generate --check
  RESULT_VARIABLE _registry_index_result)
if(NOT _registry_index_result EQUAL 0)
  message(FATAL_ERROR "Generated actor registry index is stale")
endif()

foreach(_resolution IN ITEMS
        "onebot-cxx.message-store|0.1.0|message_store-linux-x86_64.so"
        "vollate.bridge|0.1.0|bridge-linux-x86_64.so")
  string(REPLACE "|" ";" _resolution_fields "${_resolution}")
  list(GET _resolution_fields 0 _actor_id)
  list(GET _resolution_fields 1 _actor_version)
  list(GET _resolution_fields 2 _actor_filename)
  execute_process(
    COMMAND "${OBCX_PYTHON_EXECUTABLE}" "${_registry_tool}" resolve
            --id "${_actor_id}" --version "${_actor_version}"
            --platform linux-x86_64
    OUTPUT_VARIABLE _resolution_output
    RESULT_VARIABLE _resolution_result)
  if(NOT _resolution_result EQUAL 0 OR
     NOT _resolution_output MATCHES "${_actor_filename}")
    message(FATAL_ERROR
            "Actor registry failed to resolve ${_actor_id} ${_actor_version}")
  endif()
endforeach()

foreach(_source IN ITEMS "${_message_store_source}" "${_bridge_source}"
                         "${_template_source}")
  if(NOT EXISTS "${_source}/CMakeLists.txt")
    message(FATAL_ERROR
            "Standalone actor repository is not checked out: ${_source}")
  endif()
endforeach()
if(NOT EXISTS "${_actor_registry_source}/generate_actor_index.py")
  message(FATAL_ERROR
          "Independent actor-registry repository is not checked out: "
          "${_actor_registry_source}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${OBCX_BUILD_DIR}" --prefix
          "${_install_dir}"
  RESULT_VARIABLE _install_result)
if(NOT _install_result EQUAL 0)
  message(FATAL_ERROR "Failed to install the OBCX V2 SDK")
endif()

foreach(_actor IN ITEMS message_store bridge template)
  if(_actor STREQUAL "message_store")
    set(_source "${_message_store_source}")
    set(_build "${_message_store_build}")
    set(_artifact "message_store")
    set(_actor_id "onebot-cxx.message-store")
    set(_run_tests ON)
  elseif(_actor STREQUAL "bridge")
    set(_source "${_bridge_source}")
    set(_build "${_bridge_build}")
    set(_artifact "bridge")
    set(_actor_id "vollate.bridge")
    set(_run_tests ON)
  else()
    set(_source "${_template_source}")
    set(_build "${_template_build}")
    set(_artifact "example")
    set(_actor_id "onebot-cxx.example")
    set(_run_tests OFF)
  endif()

  set(_actor_test_args)
  if(_actor STREQUAL "message_store")
    list(APPEND _actor_test_args -DOBCX_MESSAGE_STORE_BUILD_TESTS=ON)
  elseif(_actor STREQUAL "bridge")
    if(NOT DEFINED _message_store_actor OR
       NOT EXISTS "${_message_store_actor}")
      message(FATAL_ERROR
              "Bridge conformance requires the installed Message Store actor")
    endif()
    list(APPEND _actor_test_args
         -DOBCX_BRIDGE_BUILD_TESTS=ON
         -DOBCX_BRIDGE_CONFORMANCE_TESTS=ON
         "-DOBCX_MESSAGE_STORE_ACTOR_PATH=${_message_store_actor}"
         "-DOBCX_BRIDGE_CONFORMANCE_INSTALL_PREFIX=${_install_dir}"
         "-DOBCX_CORE_SOURCE_DIR=${OBCX_SOURCE_DIR}")
  endif()

  execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${_source}" -B "${_build}"
            -DCMAKE_BUILD_TYPE=${OBCX_CONSUMER_BUILD_TYPE}
            -DBUILD_TESTING=${_run_tests} ${_actor_test_args}
            "-DCMAKE_PREFIX_PATH=${_install_dir};${OBCX_DEPENDENCY_PREFIX}"
            ${_consumer_toolchain_args}
    RESULT_VARIABLE _configure_result)
  if(NOT _configure_result EQUAL 0)
    message(FATAL_ERROR
            "Failed to configure standalone ${_actor} against the V2 SDK")
  endif()

  execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${_build}" -j2
    RESULT_VARIABLE _build_result)
  if(NOT _build_result EQUAL 0)
    message(FATAL_ERROR
            "Failed to build standalone ${_actor} against the V2 SDK")
  endif()

  if(_run_tests)
    execute_process(
      COMMAND "${OBCX_CTEST_COMMAND}" --test-dir "${_build}" -N
      OUTPUT_VARIABLE _actor_inventory
      ERROR_VARIABLE _actor_inventory_error
      RESULT_VARIABLE _actor_inventory_result)
    if(NOT _actor_inventory_result EQUAL 0)
      message(FATAL_ERROR
              "Unable to inspect standalone ${_actor} test inventory: ${_actor_inventory_error}")
    endif()
    if(_actor STREQUAL "message_store" AND
       NOT _actor_inventory MATCHES "message_store_smoke")
      message(FATAL_ERROR
              "Standalone message_store did not register its owned suite")
    elseif(_actor STREQUAL "bridge" AND
           (NOT _actor_inventory MATCHES "BridgeActorTest" OR
            NOT _actor_inventory MATCHES "BridgeInstalledPipelineSmoke" OR
            NOT _actor_inventory MATCHES "BridgeInstalledReloadSmoke"))
      message(FATAL_ERROR
              "Standalone bridge did not register its complete owned suite")
    endif()
  endif()

  # Install every actor into the same relocatable deployment prefix as OBCX.
  # This exercises the layout operators ship instead of loading artifacts from
  # their build trees or relying on LD_LIBRARY_PATH.
  set(_actor_install "${_install_dir}")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${_build}" --prefix
            "${_actor_install}"
    RESULT_VARIABLE _actor_install_result)
  if(NOT _actor_install_result EQUAL 0)
    message(FATAL_ERROR "Standalone ${_actor} install failed")
  endif()
  file(GLOB _installed_artifacts
       "${_actor_install}/lib/obcx/actors/${_artifact}.*")
  list(LENGTH _installed_artifacts _installed_artifact_count)
  if(NOT _installed_artifact_count EQUAL 1 OR
     NOT EXISTS
       "${_actor_install}/share/obcx/actors/${_actor_id}/actor.toml")
    message(FATAL_ERROR
            "Standalone ${_actor} did not install one actor and actor.toml")
  endif()
  list(GET _installed_artifacts 0 _installed_artifact)
  if(_actor STREQUAL "message_store")
    set(_message_store_actor "${_installed_artifact}")
  elseif(_actor STREQUAL "bridge")
    set(_bridge_actor "${_installed_artifact}")
  endif()

  if(_run_tests)
    execute_process(
      COMMAND "${OBCX_CTEST_COMMAND}" --test-dir "${_build}"
              --output-on-failure --no-tests=error
      RESULT_VARIABLE _test_result)
    if(NOT _test_result EQUAL 0)
      message(FATAL_ERROR "Standalone ${_actor} V2 tests failed")
    endif()
  endif()
endforeach()

set(_installed_validation_config
    "${_conformance_root}/installed-name-lookup.toml")
file(WRITE "${_installed_validation_config}" "
[actors.message_store]
library = \"message_store\"
enabled = true

[pipelines.installed]
source = \"obcx::core::events::RawMessageEvent\"

[[pipelines.installed.stages]]
name = \"persist\"
actor = \"message_store\"
input = \"obcx::core::events::RawMessageEvent\"
output = \"obcx::message_store::events::MessageStored\"
")
execute_process(
  COMMAND "${_install_dir}/bin/obcx" --validate-config
          "${_installed_validation_config}"
  WORKING_DIRECTORY "${_conformance_root}"
  OUTPUT_VARIABLE _installed_validation_output
  ERROR_VARIABLE _installed_validation_error
  RESULT_VARIABLE _installed_validation_result)
if(NOT _installed_validation_result EQUAL 0 OR
   NOT _installed_validation_output MATCHES
       "Configuration and actor contracts are valid")
  message(FATAL_ERROR
          "Installed name-based actor lookup failed: "
          "${_installed_validation_output}${_installed_validation_error}")
endif()

execute_process(
  COMMAND "${_install_dir}/bin/obcx" --version
  OUTPUT_VARIABLE _installed_obcx_version
  ERROR_VARIABLE _installed_obcx_error
  RESULT_VARIABLE _installed_obcx_result)
if(NOT _installed_obcx_result EQUAL 0 OR
   NOT _installed_obcx_version MATCHES
       "OBCX Robot Framework v[0-9]+\\.[0-9]+\\.[0-9]+")
  message(FATAL_ERROR
          "Installed OBCX failed to start: ${_installed_obcx_error}")
endif()

foreach(_bridge_smoke IN ITEMS standalone_actor_pipeline_smoke
                               standalone_actor_reload_smoke)
  if(NOT EXISTS "${_install_dir}/libexec/obcx/${_bridge_smoke}")
    message(FATAL_ERROR
            "Bridge did not install owned conformance smoke: ${_bridge_smoke}")
  endif()
endforeach()
