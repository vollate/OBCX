# QQ to Telegram Plugin CMakeLists.txt

# Find required packages
find_package(tomlplusplus REQUIRED)

# Create the QQ to TG plugin shared library
add_library(
  qq_to_tg_plugin SHARED
  qq_to_tg_plugin.cpp
  qq_to_tg_plugin.hpp
  # BridgeBot dependencies
  ../BridgeBot/config.cpp
  ../BridgeBot/qq_handler.cpp
  ../BridgeBot/telegram_handler.cpp
  ../BridgeBot/media_processor.cpp
  ../BridgeBot/path_manager.cpp
  ../BridgeBot/database_manager.cpp
  ../BridgeBot/retry_queue_manager.cpp
  ../BridgeBot/telegram/telegram_media_processor.cpp
  ../BridgeBot/telegram/telegram_message_formatter.cpp
  ../BridgeBot/telegram/telegram_command_handler.cpp
  ../BridgeBot/telegram/telegram_event_handler.cpp
  ../BridgeBot/qq/qq_media_processor.cpp)

# Set the output name to match plugin loading expectations
set_target_properties(
  qq_to_tg_plugin
  PROPERTIES OUTPUT_NAME "qq_to_tg"
             PREFIX "" # No lib prefix
             POSITION_INDEPENDENT_CODE ON
             CXX_STANDARD 20
             CXX_STANDARD_REQUIRED ON)

# Include directories
target_include_directories(
  qq_to_tg_plugin PRIVATE ${CMAKE_SOURCE_DIR}/examples
                          ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)

# Link against OBCX core and BridgeBot dependencies
target_link_libraries(
  qq_to_tg_plugin
  PRIVATE obcx_core tomlplusplus::tomlplusplus unofficial::sqlite3::sqlite3
          OpenSSL::SSL OpenSSL::Crypto)

# Set the output directory to build/plugins
set_target_properties(qq_to_tg_plugin PROPERTIES LIBRARY_OUTPUT_DIRECTORY
                                                 ${CMAKE_BINARY_DIR}/plugins)

