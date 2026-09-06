add_test(
  NAME actor_sdk_v2_smoke
  COMMAND
    ${CMAKE_COMMAND} -DOBCX_BUILD_DIR=${CMAKE_BINARY_DIR}
    -DOBCX_SOURCE_DIR=${CMAKE_SOURCE_DIR}
    "-DOBCX_DEPENDENCY_PREFIX=${CMAKE_PREFIX_PATH}"
    "-DOBCX_CONSUMER_C_FLAGS=${CMAKE_C_FLAGS}"
    "-DOBCX_CONSUMER_CXX_FLAGS=${CMAKE_CXX_FLAGS}"
    "-DOBCX_CONSUMER_EXE_LINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS}"
    "-DOBCX_CONSUMER_SHARED_LINKER_FLAGS=${CMAKE_SHARED_LINKER_FLAGS}" -P
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_v2_sdk_smoke.cmake)
set_tests_properties(actor_sdk_v2_smoke PROPERTIES LABELS
                                                   "contract;installed-sdk")

foreach(_kind IN ITEMS common onebot11 telegram)
  add_test(NAME bot_sdk_${_kind}_isolation
    COMMAND ${CMAKE_COMMAND}
      -DOBCX_SOURCE_DIR=${CMAKE_SOURCE_DIR} -DOBCX_BUILD_DIR=${CMAKE_BINARY_DIR}
      -DOBCX_BOT_SDK_KIND=${_kind} -DOBCX_CTEST_COMMAND=${CMAKE_CTEST_COMMAND}
      "-DOBCX_DEPENDENCY_PREFIX=${CMAKE_PREFIX_PATH}"
      "-DOBCX_CONSUMER_CXX_FLAGS=${CMAKE_CXX_FLAGS}"
      "-DOBCX_CONSUMER_EXE_LINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS}"
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_bot_sdk_isolation.cmake)
  set_tests_properties(bot_sdk_${_kind}_isolation PROPERTIES
    LABELS "contract;installed-sdk;isolation" TIMEOUT 300)
endforeach()

option(OBCX_ENABLE_CROSS_REPO_ACTOR_TESTS
       "Build checked-out standalone actor repositories against this SDK" OFF)
if(OBCX_ENABLE_CROSS_REPO_ACTOR_TESTS)
  add_test(
    NAME standalone_actor_v2_repositories
    COMMAND
      ${CMAKE_COMMAND} -DOBCX_BUILD_DIR=${CMAKE_BINARY_DIR}
      -DOBCX_SOURCE_DIR=${CMAKE_SOURCE_DIR}
      -DOBCX_CTEST_COMMAND=${CMAKE_CTEST_COMMAND}
      -DOBCX_PYTHON_EXECUTABLE=${Python3_EXECUTABLE}
      -DOBCX_CONSUMER_BUILD_TYPE=${CMAKE_BUILD_TYPE}
      "-DOBCX_DEPENDENCY_PREFIX=${CMAKE_PREFIX_PATH}"
      "-DOBCX_CONSUMER_C_FLAGS=${CMAKE_C_FLAGS}"
      "-DOBCX_CONSUMER_CXX_FLAGS=${CMAKE_CXX_FLAGS}"
      "-DOBCX_CONSUMER_EXE_LINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS}"
      "-DOBCX_CONSUMER_SHARED_LINKER_FLAGS=${CMAKE_SHARED_LINKER_FLAGS}" -P
      ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_standalone_actor_v2_repositories.cmake
  )
  set_tests_properties(
    standalone_actor_v2_repositories
    PROPERTIES LABELS
               "conformance;integration;cross-repository;actor-package;slow"
               TIMEOUT
               420)
endif()

add_test(
  NAME validate_config_cli
  COMMAND
    ${CMAKE_COMMAND} -DOBCX_EXECUTABLE=$<TARGET_FILE:obcx>
    -DOBCX_TEST_ACTOR=$<TARGET_FILE:obcx_test_actor_v2> -P
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_validate_config_cli.cmake)
set_tests_properties(validate_config_cli
                     PROPERTIES LABELS "full;integration;actor-runtime;cli")

add_test(
  NAME test_inventory_ownership
  COMMAND
    ${CMAKE_COMMAND} -DOBCX_BUILD_DIR=${CMAKE_BINARY_DIR}
    -DOBCX_SOURCE_DIR=${CMAKE_SOURCE_DIR}
    -DOBCX_CTEST_COMMAND=${CMAKE_CTEST_COMMAND}
    -DOBCX_BRIDGE_ACTOR=$<TARGET_FILE:bridge_actor>
    -DOBCX_MESSAGE_STORE_ACTOR=$<TARGET_FILE:message_store_actor> -P
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_test_inventory_ownership.cmake)
set_tests_properties(
  test_inventory_ownership
  PROPERTIES LABELS "full;architecture;actor-package;test-ownership")
