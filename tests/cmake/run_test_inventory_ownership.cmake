if(NOT DEFINED OBCX_BUILD_DIR OR NOT DEFINED OBCX_CTEST_COMMAND OR
   NOT DEFINED OBCX_SOURCE_DIR OR NOT DEFINED OBCX_BRIDGE_ACTOR OR
   NOT DEFINED OBCX_MESSAGE_STORE_ACTOR)
  message(FATAL_ERROR "Root test inventory inputs are required")
endif()

if(NOT EXISTS "${OBCX_BRIDGE_ACTOR}" OR
   NOT EXISTS "${OBCX_MESSAGE_STORE_ACTOR}")
  message(FATAL_ERROR
          "Embedded actor artifacts were not built for root inventory check")
endif()

execute_process(
  COMMAND "${OBCX_CTEST_COMMAND}" --test-dir "${OBCX_BUILD_DIR}"
          --show-only=json-v1
  OUTPUT_VARIABLE _inventory
  ERROR_VARIABLE _inventory_error
  RESULT_VARIABLE _inventory_result)
if(NOT _inventory_result EQUAL 0)
  message(FATAL_ERROR
          "Unable to inspect root CTest inventory: ${_inventory_error}")
endif()

set(_actor_owned_suites
    BridgeActorTest
    BridgeDatabaseSchemaTest
    RetryQueueManagerTest
    BridgeHandlerRepositoryTest
    ReceivedMessageRepositoryTest
    BridgeMessageEventAdapterTest
    PathManagerTest
    ImageUrlValidatorTest
    PhotoNormalizerTest
    BridgeDbRuntimeTest
    message_store_smoke)
foreach(_suite IN LISTS _actor_owned_suites)
  if(_inventory MATCHES "\"name\"[ \t\r\n]*:[ \t\r\n]*\"${_suite}")
    message(FATAL_ERROR
            "Actor-owned suite leaked into root CTest inventory: ${_suite}")
  endif()
endforeach()

foreach(_registration IN ITEMS actor_fixtures.cmake integration_tests.cmake
                               unit_tests.cmake)
  file(READ "${OBCX_SOURCE_DIR}/tests/cmake/${_registration}"
       _registration_content)
  if(_registration_content MATCHES
     "local_actor/.*/(actor|dependency|include|tests)/")
    message(FATAL_ERROR
            "Root test registration compiles an actor-owned source: ${_registration}")
  endif()
endforeach()

file(GLOB_RECURSE _root_behavior_sources
     "${OBCX_SOURCE_DIR}/tests/cpp/*.cpp"
     "${OBCX_SOURCE_DIR}/tests/fixtures/*.cpp"
     "${OBCX_SOURCE_DIR}/tests/support/*.hpp")
foreach(_source IN LISTS _root_behavior_sources)
  file(READ "${_source}" _contents)
  foreach(_forbidden IN ITEMS
          "bridge_forwarder.hpp"
          "local_actor/obcx-actor-bridge/include"
          "local_actor/obcx-actor-message-store/include"
          "local_actor/obcx-actor-bridge/dependency"
          "local_actor/obcx-actor-message-store/src")
    string(FIND "${_contents}" "${_forbidden}" _found)
    if(NOT _found EQUAL -1)
      message(FATAL_ERROR
              "Root behavior source depends on actor-private content: ${_source}: ${_forbidden}")
    endif()
  endforeach()
endforeach()

message(STATUS "Root CTest inventory contains core-owned suites only")
