cmake_minimum_required(VERSION 3.30)
project(obcx_v2_sdk_smoke LANGUAGES CXX)

find_package(obcx-sdk CONFIG REQUIRED)
include(OBCXActor)

obcx_add_actor(sdk_v2
    SOURCES actor.cpp
    OUTPUT_NAME sdk_v2_fixture)

add_executable(sdk_v2_smoke smoke.cpp)
target_link_libraries(sdk_v2_smoke PRIVATE obcx::obcx_core)
target_compile_features(sdk_v2_smoke PRIVATE cxx_std_26)
target_compile_options(sdk_v2_smoke PRIVATE -freflection)
add_dependencies(sdk_v2_smoke sdk_v2_actor)

install(TARGETS sdk_v2_smoke RUNTIME DESTINATION bin)
