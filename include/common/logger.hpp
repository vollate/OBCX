#pragma once

#include <memory>
#include <optional>
#include <spdlog/spdlog.h>
#include <string>

#include "common/log_messages.hpp" // NOLINT
#include "tui/tui_sink.hpp"

#ifdef OBCX_DEBUG_TRACE
#include <fmt/color.h>
#include <fmt/format.h>
#endif

namespace obcx::common {

/**
 * \if CHINESE
 * @brief 日志管理器，提供统一的日志接口
 * \endif
 * \if ENGLISH
 * @brief Logger manager, providing a unified logging interface.
 * \endif
 */
class Logger {
public:
  /**
   * \if CHINESE
   * @brief 初始化日志系统
   * @param level 日志级别
   * @param log_file 日志文件路径 (可选)
   * \endif
   * \if ENGLISH
   * @brief Initializes the logging system.
   * @param level The logging level.
   * @param log_file The path to the log file (optional).
   * \endif
   */
  static void initialize(spdlog::level::level_enum level = spdlog::level::info,
                         const std::string &log_file = "", bool use_tui = true);

  /**
   * \if CHINESE
   * @brief 获取默认日志器
   * \endif
   * \if ENGLISH
   * @brief Gets the default logger.
   * \endif
   */
  static auto get() -> std::shared_ptr<spdlog::logger>;

  /**
   * \if CHINESE
   * @brief 获取指定名称的日志器
   * \endif
   * \if ENGLISH
   * @brief Gets a logger with a specific name.
   * \endif
   */
  static auto get(const std::string &name) -> std::shared_ptr<spdlog::logger>;

  /**
   * \if CHINESE
   * @brief 设置日志级别
   * \endif
   * \if ENGLISH
   * @brief Sets the logging level.
   * \endif
   */
  static void set_level(spdlog::level::level_enum level);

  /**
   * \if CHINESE
   * @brief 从字符串解析日志级别
   * @param level_str 日志级别字符串 (trace, debug, info, warn, error, critical,
   * off)
   * @return 解析成功返回日志级别，失败返回std::nullopt
   * \endif
   * \if ENGLISH
   * @brief Parse log level from string
   * @param level_str Log level string (trace, debug, info, warn, error,
   * critical, off)
   * @return Log level if parsing succeeds, std::nullopt otherwise
   * \endif
   */
  static auto parse_level(const std::string &level_str)
      -> std::optional<spdlog::level::level_enum>;

  /**
   * \if CHINESE
   * @brief 从环境变量获取日志级别
   * @param env_var 环境变量名称 (默认: OBCX_LOG_LEVEL)
   * @param default_level 默认日志级别 (默认: info)
   * @return 日志级别
   * \endif
   * \if ENGLISH
   * @brief Get log level from environment variable
   * @param env_var Environment variable name (default: OBCX_LOG_LEVEL)
   * @param default_level Default log level (default: info)
   * @return Log level
   * \endif
   */
  static auto get_level_from_env(
      const std::string &env_var = "OBCX_LOG_LEVEL",
      spdlog::level::level_enum default_level = spdlog::level::info)
      -> spdlog::level::level_enum;

  /**
   * \if CHINESE
   * @brief 刷新所有日志器
   * \endif
   * \if ENGLISH
   * @brief Flushes all loggers.
   * \endif
   */
  static void flush();

  /**
   * \if CHINESE
   * @brief 获取TUI sink实例
   * @return TUI sink的shared_ptr，如果未初始化则返回nullptr
   * \endif
   * \if ENGLISH
   * @brief Get the TUI sink instance
   * @return shared_ptr to TUI sink, nullptr if not initialized
   * \endif
   */
  static auto get_tui_sink() -> std::shared_ptr<tui_sink_mt>;

private:
  static std::shared_ptr<spdlog::logger> default_logger_;
  static std::shared_ptr<tui_sink_mt> tui_sink_;
  static bool initialized_;
};

#ifdef OBCX_DEBUG_TRACE
#define OBCX_LOG_IMPL(__level, __fmt_str, ...)                                 \
  do {                                                                         \
    if (obcx::common::Logger::get()->should_log(spdlog::level::__level)) {     \
      obcx::common::Logger::get()->log(                                        \
          spdlog::level::__level,                                              \
          fmt::format("{} " __fmt_str,                                         \
                      fmt::styled(fmt::format("[{}:{}]", __FILE__, __LINE__),  \
                                  fmt::fg(fmt::color::dark_orange)),           \
                      ##__VA_ARGS__));                                         \
    }                                                                          \
  } while (false)

#define OBCX_TRACE(__fmt, ...) OBCX_LOG_IMPL(trace, __fmt, ##__VA_ARGS__)
#define OBCX_DEBUG(__fmt, ...) OBCX_LOG_IMPL(debug, __fmt, ##__VA_ARGS__)
#define OBCX_INFO(__fmt, ...) OBCX_LOG_IMPL(info, __fmt, ##__VA_ARGS__)
#define OBCX_WARN(__fmt, ...) OBCX_LOG_IMPL(warn, __fmt, ##__VA_ARGS__)
#define OBCX_ERROR(__fmt, ...) OBCX_LOG_IMPL(err, __fmt, ##__VA_ARGS__)
#define OBCX_CRITICAL(__fmt, ...) OBCX_LOG_IMPL(critical, __fmt, ##__VA_ARGS__)
#else
#define OBCX_TRACE(...) obcx::common::Logger::get()->trace(__VA_ARGS__)
#define OBCX_DEBUG(...) obcx::common::Logger::get()->debug(__VA_ARGS__)
#define OBCX_INFO(...) obcx::common::Logger::get()->info(__VA_ARGS__)
#define OBCX_WARN(...) obcx::common::Logger::get()->warn(__VA_ARGS__)
#define OBCX_ERROR(...) obcx::common::Logger::get()->error(__VA_ARGS__)
#define OBCX_CRITICAL(...) obcx::common::Logger::get()->critical(__VA_ARGS__)
#endif

#define OBCX_KEY_LOG_IMPL(__level, __key, ...)                                 \
  do {                                                                         \
    if (obcx::common::Logger::get()->should_log(spdlog::level::__level)) {     \
      std::string __msg =                                                      \
          obcx::common::LogMessages::format_message(__key, ##__VA_ARGS__);     \
      obcx::common::Logger::get()->log(spdlog::level::__level, __msg);         \
    }                                                                          \
  } while (false)

#define OBCX_KEY_TRACE(__key, ...)                                             \
  OBCX_KEY_LOG_IMPL(trace, __key, ##__VA_ARGS__)
#define OBCX_KEY_DEBUG(__key, ...)                                             \
  OBCX_KEY_LOG_IMPL(debug, __key, ##__VA_ARGS__)
#define OBCX_KEY_INFO(__key, ...) OBCX_KEY_LOG_IMPL(info, __key, ##__VA_ARGS__)
#define OBCX_KEY_WARN(__key, ...) OBCX_KEY_LOG_IMPL(warn, __key, ##__VA_ARGS__)
#define OBCX_KEY_ERROR(__key, ...) OBCX_KEY_LOG_IMPL(err, __key, ##__VA_ARGS__)
#define OBCX_KEY_CRITICAL(__key, ...)                                          \
  OBCX_KEY_LOG_IMPL(critical, __key, ##__VA_ARGS__)

} // namespace obcx::common

/*
 * \if CHINESE
 * Plugin专用日志宏定义
 * 使用方式：PLUGIN_INFO(get_name(), "message")
 * \endif
 * \if ENGLISH
 * Plugin-specific logging macros - allow plugins to use their own logger name
 * Usage: PLUGIN_INFO(get_name(), "message")
 * \endif
 */

#ifdef OBCX_DEBUG_TRACE
#define PLUGIN_LOG_IMPL(__plugin_name, __level, __fmt_str, ...)                \
  do {                                                                         \
    auto __logger = obcx::common::Logger::get(__plugin_name);                  \
    if (__logger && __logger->should_log(spdlog::level::__level)) {            \
      __logger->log(                                                           \
          spdlog::level::__level,                                              \
          fmt::format("{} " __fmt_str,                                         \
                      fmt::styled(fmt::format("[{}:{}]", __FILE__, __LINE__),  \
                                  fmt::fg(fmt::color::dark_orange)),           \
                      ##__VA_ARGS__));                                         \
    }                                                                          \
  } while (false)

#define PLUGIN_TRACE(__plugin_name, __fmt, ...)                                \
  PLUGIN_LOG_IMPL(__plugin_name, trace, __fmt, ##__VA_ARGS__)
#define PLUGIN_DEBUG(__plugin_name, __fmt, ...)                                \
  PLUGIN_LOG_IMPL(__plugin_name, debug, __fmt, ##__VA_ARGS__)
#define PLUGIN_INFO(__plugin_name, __fmt, ...)                                 \
  PLUGIN_LOG_IMPL(__plugin_name, info, __fmt, ##__VA_ARGS__)
#define PLUGIN_WARN(__plugin_name, __fmt, ...)                                 \
  PLUGIN_LOG_IMPL(__plugin_name, warn, __fmt, ##__VA_ARGS__)
#define PLUGIN_ERROR(__plugin_name, __fmt, ...)                                \
  PLUGIN_LOG_IMPL(__plugin_name, err, __fmt, ##__VA_ARGS__)
#define PLUGIN_CRITICAL(__plugin_name, __fmt, ...)                             \
  PLUGIN_LOG_IMPL(__plugin_name, critical, __fmt, ##__VA_ARGS__)

#else
#define PLUGIN_TRACE(__plugin_name, ...)                                       \
  do {                                                                         \
    auto __logger = obcx::common::Logger::get(__plugin_name);                  \
    if (__logger)                                                              \
      __logger->trace(__VA_ARGS__);                                            \
  } while (false)

#define PLUGIN_DEBUG(__plugin_name, ...)                                       \
  do {                                                                         \
    auto __logger = obcx::common::Logger::get(__plugin_name);                  \
    if (__logger)                                                              \
      __logger->debug(__VA_ARGS__);                                            \
  } while (false)

#define PLUGIN_INFO(__plugin_name, ...)                                        \
  do {                                                                         \
    auto __logger = obcx::common::Logger::get(__plugin_name);                  \
    if (__logger)                                                              \
      __logger->info(__VA_ARGS__);                                             \
  } while (false)

#define PLUGIN_WARN(__plugin_name, ...)                                        \
  do {                                                                         \
    auto __logger = obcx::common::Logger::get(__plugin_name);                  \
    if (__logger)                                                              \
      __logger->warn(__VA_ARGS__);                                             \
  } while (false)

#define PLUGIN_ERROR(__plugin_name, ...)                                       \
  do {                                                                         \
    auto __logger = obcx::common::Logger::get(__plugin_name);                  \
    if (__logger)                                                              \
      __logger->error(__VA_ARGS__);                                            \
  } while (false)

#define PLUGIN_CRITICAL(__plugin_name, ...)                                    \
  do {                                                                         \
    auto __logger = obcx::common::Logger::get(__plugin_name);                  \
    if (__logger)                                                              \
      __logger->critical(__VA_ARGS__);                                         \
  } while (false)

#endif

/*
 * \if CHINESE
 * Plugin专用国际化日志宏定义
 * 使用方式：PLUGIN_KEY_INFO(get_name(), LogMessageKey::XXX, args...)
 * \endif
 * \if ENGLISH
 * Plugin-specific internationalized logging macro definitions
 * Usage: PLUGIN_KEY_INFO(get_name(), LogMessageKey::XXX, args...)
 * \endif
 */
#define PLUGIN_KEY_LOG_IMPL(__plugin_name, __level, __key, ...)                \
  do {                                                                         \
    auto __logger = obcx::common::Logger::get(__plugin_name);                  \
    if (__logger && __logger->should_log(spdlog::level::__level)) {            \
      std::string __msg =                                                      \
          obcx::common::LogMessages::format_message(__key, ##__VA_ARGS__);     \
      __logger->log(spdlog::level::__level, __msg);                            \
    }                                                                          \
  } while (false)

#define PLUGIN_KEY_TRACE(__plugin_name, __key, ...)                            \
  PLUGIN_KEY_LOG_IMPL(__plugin_name, trace, __key, ##__VA_ARGS__)
#define PLUGIN_KEY_DEBUG_TRACE(__plugin_name, __key, ...)                      \
  PLUGIN_KEY_LOG_IMPL(__plugin_name, debug, __key, ##__VA_ARGS__)
#define PLUGIN_KEY_INFO(__plugin_name, __key, ...)                             \
  PLUGIN_KEY_LOG_IMPL(__plugin_name, info, __key, ##__VA_ARGS__)
#define PLUGIN_KEY_WARN(__plugin_name, __key, ...)                             \
  PLUGIN_KEY_LOG_IMPL(__plugin_name, warn, __key, ##__VA_ARGS__)
#define PLUGIN_KEY_ERROR(__plugin_name, __key, ...)                            \
  PLUGIN_KEY_LOG_IMPL(__plugin_name, err, __key, ##__VA_ARGS__)
#define PLUGIN_KEY_CRITICAL(__plugin_name, __key, ...)                         \
  PLUGIN_KEY_LOG_IMPL(__plugin_name, critical, __key, ##__VA_ARGS__)
