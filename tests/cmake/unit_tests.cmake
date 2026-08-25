function(obcx_add_gtest name labels)
  string(REPLACE ";" "\\;" escaped_labels "${labels}")
  add_executable(${name} cpp/${name}.cpp)
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(${name} PRIVATE obcx_core GTest::gtest_main
                                        GTest::gmock_main)
  gtest_discover_tests(${name} DISCOVERY_MODE PRE_TEST
                       PROPERTIES LABELS "${escaped_labels}")
endfunction()

obcx_add_gtest(actor_api_test "unit;actor-runtime")
obcx_add_gtest(actor_package_stager_test "unit;actor-runtime;staging")
obcx_add_gtest(actor_asio_interop_test "unit;actor-runtime;concurrency")
obcx_add_gtest(actor_config_test "unit;actor-runtime;configuration")
obcx_add_gtest(actor_manager_test "unit;actor-runtime;loader")
obcx_add_gtest(actor_task_test "unit;actor-runtime;coroutine")
obcx_add_gtest(actor_work_stealing_executor_test
               "unit;actor-runtime;concurrency")
obcx_add_gtest(blocking_executor_test "unit;actor-runtime;concurrency")
obcx_add_gtest(bot_component_runtime_test
               "unit;bot-runtime;component;lifecycle;concurrency")
obcx_add_gtest(bot_event_component_test
               "unit;bot-runtime;component;ingress")
obcx_add_gtest(bot_installation_assembler_test
               "unit;bot-runtime;component;configuration")
obcx_add_gtest(bot_installation_config_test
               "unit;bot-runtime;configuration;security")
obcx_add_gtest(bot_operation_component_test
               "unit;bot-runtime;component;telegram;onebot11")
obcx_add_gtest(bot_operation_dispatcher_test "unit;bot-runtime;dispatch")
obcx_add_gtest(bot_operation_response_parser_test
               "unit;bot-runtime;telegram;onebot11")
obcx_add_gtest(bot_operation_types_test "unit;bot-runtime;contract")
obcx_add_gtest(cli_handler_test "unit;cli;actor-runtime")
obcx_add_gtest(command_coordinator_test
               "unit;actor-runtime;routing;bot-runtime;concurrency")
obcx_add_gtest(command_platform_adapter_test "unit;actor-runtime;bot-runtime")
obcx_add_gtest(db_manager_test "unit;database")
obcx_add_gtest(http_client_timeout_test "integration;network")
obcx_add_gtest(message_event_ingress_test "unit;bot-runtime")
obcx_add_gtest(native_actor_scheduler_test "unit;actor-runtime;concurrency")
obcx_add_gtest(orchestrator_test "unit;actor-runtime;routing")
obcx_add_gtest(reflected_actor_test "unit;actor-runtime;reflection")
obcx_add_gtest(runtime_reload_controller_test
               "integration;actor-runtime;reload")
obcx_add_gtest(runtime_generation_test "integration;actor-runtime;reload")
obcx_add_gtest(runtime_thread_budget_test "unit;actor-runtime;configuration")
obcx_add_gtest(telegram_formatting_test "unit;telegram")
obcx_add_gtest(telegram_multipart_test "unit;network;telegram")
obcx_add_gtest(tui_layout_test "unit;tui")

obcx_add_gtest(websocket_queue_test "fast;integration;network")
obcx_add_gtest(websocket_timeout_test "fast;unit;network")

add_dependencies(actor_package_stager_test obcx_private_actor_v1
                 obcx_private_actor_v2)
target_compile_definitions(
  actor_package_stager_test
  PRIVATE OBCX_PRIVATE_ACTOR_V1="$<TARGET_FILE:obcx_private_actor_v1>"
          OBCX_PRIVATE_ACTOR_V2="$<TARGET_FILE:obcx_private_actor_v2>")

add_dependencies(
  runtime_generation_test obcx_test_actor_v2 obcx_activation_failure_actor
  obcx_private_actor_v1 obcx_private_actor_v2)
target_compile_definitions(
  runtime_generation_test
  PRIVATE
    OBCX_TEST_ACTOR_V2_LIBRARY="$<TARGET_FILE:obcx_test_actor_v2>"
    OBCX_ACTIVATION_FAILURE_ACTOR="$<TARGET_FILE:obcx_activation_failure_actor>"
    OBCX_PRIVATE_ACTOR_V1="$<TARGET_FILE:obcx_private_actor_v1>"
    OBCX_PRIVATE_ACTOR_V2="$<TARGET_FILE:obcx_private_actor_v2>")

add_dependencies(
  runtime_reload_controller_test obcx_reload_lifecycle_actor
  obcx_private_actor_v1 obcx_private_actor_v2 obcx_rebuilt_actor_v1
  obcx_rebuilt_actor_v2)
target_compile_definitions(
  runtime_reload_controller_test
  PRIVATE
    OBCX_RELOAD_LIFECYCLE_ACTOR="$<TARGET_FILE:obcx_reload_lifecycle_actor>"
    OBCX_PRIVATE_ACTOR_V1="$<TARGET_FILE:obcx_private_actor_v1>"
    OBCX_PRIVATE_ACTOR_V2="$<TARGET_FILE:obcx_private_actor_v2>"
    OBCX_REBUILT_ACTOR_V1="$<TARGET_FILE:obcx_rebuilt_actor_v1>"
    OBCX_REBUILT_ACTOR_V2="$<TARGET_FILE:obcx_rebuilt_actor_v2>"
)

add_dependencies(
  actor_manager_test
  obcx_multiple_inheritance_actor
  obcx_invalid_actor
  obcx_test_actor_v2
  obcx_legacy_v2_actor
  obcx_missing_v2_factory_actor
  obcx_unsupported_actor
  obcx_contract_missing
  obcx_contract_null
  obcx_contract_invalid_json
  obcx_contract_unsupported_schema
  obcx_contract_name_mismatch
  obcx_contract_duplicate_input
  obcx_contract_outputs
  obcx_contract_malformed_input
  obcx_contract_commands_not_array
  obcx_contract_duplicate_command
  obcx_contract_unsorted_command
  obcx_contract_command_callable
  obcx_contract_command_unsupported_input
  obcx_contract_command_invalid_name
  obcx_contract_command_invalid_pattern
  obcx_contract_command_matcher_callable
  obcx_contract_command_matcher_kind
  obcx_contract_command_pattern_too_large
  obcx_contract_collection_callable
  obcx_contract_collection_duplicate_field
  obcx_contract_collection_duplicate_type
  obcx_contract_collection_invalid_alternative
  obcx_contract_collection_unknown_unique_field
  obcx_contract_collection_unknown_reference)
target_compile_definitions(
  actor_manager_test
  PRIVATE
    OBCX_TEST_ACTOR_DIRECTORY="$<TARGET_FILE_DIR:obcx_test_actor_v2>"
    OBCX_TEST_MULTIPLE_INHERITANCE_ACTOR_LIBRARY="$<TARGET_FILE:obcx_multiple_inheritance_actor>"
    OBCX_TEST_INVALID_ACTOR_LIBRARY="$<TARGET_FILE:obcx_invalid_actor>"
    OBCX_TEST_ACTOR_V2_LIBRARY="$<TARGET_FILE:obcx_test_actor_v2>"
    OBCX_TEST_LEGACY_V2_ACTOR_LIBRARY="$<TARGET_FILE:obcx_legacy_v2_actor>"
    OBCX_TEST_MISSING_V2_FACTORY_LIBRARY="$<TARGET_FILE:obcx_missing_v2_factory_actor>"
    OBCX_TEST_UNSUPPORTED_ACTOR_LIBRARY="$<TARGET_FILE:obcx_unsupported_actor>"
    OBCX_TEST_CONTRACT_MISSING_LIBRARY="$<TARGET_FILE:obcx_contract_missing>"
    OBCX_TEST_CONTRACT_NULL_LIBRARY="$<TARGET_FILE:obcx_contract_null>"
    OBCX_TEST_CONTRACT_INVALID_JSON_LIBRARY="$<TARGET_FILE:obcx_contract_invalid_json>"
    OBCX_TEST_CONTRACT_SCHEMA_LIBRARY="$<TARGET_FILE:obcx_contract_unsupported_schema>"
    OBCX_TEST_CONTRACT_NAME_LIBRARY="$<TARGET_FILE:obcx_contract_name_mismatch>"
    OBCX_TEST_CONTRACT_DUPLICATE_LIBRARY="$<TARGET_FILE:obcx_contract_duplicate_input>"
    OBCX_TEST_CONTRACT_OUTPUTS_LIBRARY="$<TARGET_FILE:obcx_contract_outputs>"
    OBCX_TEST_CONTRACT_MALFORMED_INPUT_LIBRARY="$<TARGET_FILE:obcx_contract_malformed_input>"
    OBCX_TEST_CONTRACT_COMMANDS_NOT_ARRAY_LIBRARY="$<TARGET_FILE:obcx_contract_commands_not_array>"
    OBCX_TEST_CONTRACT_DUPLICATE_COMMAND_LIBRARY="$<TARGET_FILE:obcx_contract_duplicate_command>"
    OBCX_TEST_CONTRACT_UNSORTED_COMMAND_LIBRARY="$<TARGET_FILE:obcx_contract_unsorted_command>"
    OBCX_TEST_CONTRACT_COMMAND_CALLABLE_LIBRARY="$<TARGET_FILE:obcx_contract_command_callable>"
    OBCX_TEST_CONTRACT_COMMAND_UNSUPPORTED_INPUT_LIBRARY="$<TARGET_FILE:obcx_contract_command_unsupported_input>"
    OBCX_TEST_CONTRACT_COMMAND_INVALID_NAME_LIBRARY="$<TARGET_FILE:obcx_contract_command_invalid_name>"
    OBCX_TEST_CONTRACT_COMMAND_INVALID_PATTERN_LIBRARY="$<TARGET_FILE:obcx_contract_command_invalid_pattern>"
    OBCX_TEST_CONTRACT_COMMAND_MATCHER_CALLABLE_LIBRARY="$<TARGET_FILE:obcx_contract_command_matcher_callable>"
    OBCX_TEST_CONTRACT_COMMAND_MATCHER_KIND_LIBRARY="$<TARGET_FILE:obcx_contract_command_matcher_kind>"
    OBCX_TEST_CONTRACT_COMMAND_PATTERN_TOO_LARGE_LIBRARY="$<TARGET_FILE:obcx_contract_command_pattern_too_large>"
    OBCX_TEST_CONTRACT_COLLECTION_CALLABLE_LIBRARY="$<TARGET_FILE:obcx_contract_collection_callable>"
    OBCX_TEST_CONTRACT_COLLECTION_DUPLICATE_FIELD_LIBRARY="$<TARGET_FILE:obcx_contract_collection_duplicate_field>"
    OBCX_TEST_CONTRACT_COLLECTION_DUPLICATE_TYPE_LIBRARY="$<TARGET_FILE:obcx_contract_collection_duplicate_type>"
    OBCX_TEST_CONTRACT_COLLECTION_INVALID_ALTERNATIVE_LIBRARY="$<TARGET_FILE:obcx_contract_collection_invalid_alternative>"
    OBCX_TEST_CONTRACT_COLLECTION_UNKNOWN_UNIQUE_FIELD_LIBRARY="$<TARGET_FILE:obcx_contract_collection_unknown_unique_field>"
    OBCX_TEST_CONTRACT_COLLECTION_UNKNOWN_REFERENCE_LIBRARY="$<TARGET_FILE:obcx_contract_collection_unknown_reference>"
)
