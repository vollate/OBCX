find_package(GTest REQUIRED)
find_package(
  Python3
  COMPONENTS Interpreter
  REQUIRED)
include(GoogleTest)

include_directories(${CMAKE_SOURCE_DIR}/include)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/reflection_compile_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/actor_fixtures.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/unit_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/python_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/integration_tests.cmake)
