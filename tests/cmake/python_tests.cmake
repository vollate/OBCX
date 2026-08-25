function(obcx_add_python_unittest name labels)
  add_test(NAME ${name} COMMAND ${Python3_EXECUTABLE} -m unittest -v
                                ${CMAKE_CURRENT_SOURCE_DIR}/python/${name}.py)
  set_tests_properties(${name} PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                                          LABELS "${labels}")
endfunction()

obcx_add_python_unittest(actor_metadata_test "contract;actor-package;metadata")
obcx_add_python_unittest(actor_package_manifest_test
                         "contract;actor-package;metadata")
obcx_add_python_unittest(actor_vcpkg_manifest_test
                         "contract;actor-package;metadata;packaging")
obcx_add_python_unittest(actor_registry_test
                         "contract;actor-package;actor-registry;publication")
obcx_add_python_unittest(actor_release_tools_test
                         "contract;actor-package;release;deployment")

obcx_add_python_unittest(actor_architecture_test "architecture;actor-runtime")
obcx_add_python_unittest(bot_component_migration_inventory_test
                         "architecture;bot-runtime;migration")
obcx_add_python_unittest(bot_configuration_inventory_test
                         "architecture;bot-runtime;configuration;security")
obcx_add_python_unittest(bot_operation_scope_test
                         "architecture;actor-runtime;bot-operation")
