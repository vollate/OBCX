#include "common/cli_handler.hpp"
#include "common/component_manager.hpp"
#include "common/config_loader.hpp"
#include "common/log_messages.hpp"
#include "common/logger.hpp"
#include "common/plugin_manager.hpp"
#include "core/qq_bot.hpp"
#include "core/tg_bot.hpp"
#include "tui/tui_app.hpp"

#include <atomic>
#include <boost/date_time/posix_time/time_formatters.hpp>
#include <boost/program_options.hpp>
#include <csignal>
#include <cstdlib>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <iostream>
#include <memory>
#include <mutex>
#include <spdlog/common.h>
#include <string>
#include <thread>
#include <vector>

using namespace obcx;
namespace po = boost::program_options;

namespace {
std::atomic_bool g_should_stop = false;

std::mutex g_stop_mtx;
std::condition_variable g_stop_cv;

constexpr int BOT_SHUTDOWN_TIMEOUT_SECONDS = 5;

void print_version() {
  fmt::print("OBCX Robot Framework v1.1.0\n");
  fmt::print("A modular bot framework supporting QQ and Telegram\n");
}

void print_help(const po::options_description &desc) {
  fmt::print("Usage: obcx [OPTIONS] [CONFIG_FILE]\n");
  fmt::print("\n");
  fmt::print("OPTIONS:\n");
  std::cout << desc << "\n";
  fmt::print("\n");
  fmt::print("CONFIG_FILE:\n");
  fmt::print("  Path to TOML configuration file (default: config.toml)\n");
}

void signal_handler(int signal) {
  bool expected = false;
  if (!g_should_stop.compare_exchange_strong(expected, true)) {
    OBCX_KEY_WARN(common::LogMessageKey::SHUTDOWN_IN_PROGRESS, signal);
    return;
  }
  OBCX_KEY_INFO(common::LogMessageKey::SHUTDOWN_SIGNAL_RECEIVED, signal);
  g_stop_cv.notify_one();
}
} // namespace

namespace obcx::common {

class MainApplication {
public:
  static auto run(int argc, char *argv[]) -> int {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    std::string config_path = "config.toml";
    bool use_tui = true;

    try {
      po::options_description desc("Options");
      desc.add_options()("help,h", "Show this help message")(
          "version,v",
          "Show version information")("config", po::value<std::string>(),
                                      "Path to TOML configuration file")(
          "no-tui", "Disable TUI, use stdout logging (useful for debuggers)");

      po::positional_options_description p;
      p.add("config", -1);

      po::variables_map vm;
      po::store(
          po::command_line_parser(argc, argv).options(desc).positional(p).run(),
          vm);
      po::notify(vm);

      if (vm.count("help")) {
        print_help(desc);
        return 0;
      }

      if (vm.count("version")) {
        print_version();
        return 0;
      }

      if (vm.count("config")) {
        config_path = vm["config"].as<std::string>();
      }

      use_tui = !vm.count("no-tui");

    } catch (const po::error &e) {
      fmt::print(std::cerr, "Error parsing arguments: {}\n", e.what());
      return 1;
    }

    // Honors OBCX_LOG_LEVEL env var; defaults to info.
    auto log_level = common::Logger::get_level_from_env();
    common::Logger::initialize(
        log_level,
        fmt::format("logs/obcx-bridge-{}.log",
                    boost::posix_time::to_iso_extended_string(
                        boost::posix_time::second_clock::local_time())),
        use_tui);

    auto &config_loader = common::ConfigLoader::instance();
    if (!config_loader.load_config(config_path)) {
      fmt::print(std::cerr, "Failed to load configuration from: {}\n",
                 config_path);
      return 1;
    }

    auto locale = config_loader.get_value<std::string>("global.locale");
    if (locale.has_value()) {
      common::LogMessages::set_locale(*locale);
      OBCX_KEY_INFO(common::LogMessageKey::LOG_LOCALE_SET, *locale);
    }

    OBCX_KEY_INFO(common::LogMessageKey::FRAMEWORK_STARTING);
    OBCX_KEY_INFO(common::LogMessageKey::CONFIG_LOADED_FROM, config_path);

    common::PluginManager plugin_manager;
    auto &component_manager = ComponentManager::instance();

    plugin_manager.add_plugin_directory("./build/plugins");

    auto bot_configs = config_loader.get_bot_configs();
    if (bot_configs.empty()) {
      OBCX_KEY_ERROR(common::LogMessageKey::NO_BOT_CONFIGS);
      return 1;
    }

    std::vector<std::unique_ptr<core::IBot>> bots;
    std::vector<std::thread> bot_threads;
    std::mutex bots_mutex;

    interface::IPlugin::set_bots(&bots, &bots_mutex);

    auto shared_task_scheduler = std::make_shared<core::TaskScheduler>();
    OBCX_KEY_INFO(common::LogMessageKey::SHARED_TASK_SCHEDULER_CREATED,
                  std::thread::hardware_concurrency());

    for (const auto &config : bot_configs) {
      if (!config.enabled) {
        OBCX_KEY_INFO(common::LogMessageKey::SKIPPING_DISABLED_BOT,
                      config.type);
        continue;
      }

      auto bot = ComponentManager::create_bot(config, shared_task_scheduler);
      if (!bot) {
        OBCX_KEY_ERROR(common::LogMessageKey::BOT_CREATE_FAILED, config.type);
        continue;
      }

      // Push to vector before setup so plugins can locate the bot during init.
      bots.push_back(std::move(bot));
      size_t bot_index = bots.size() - 1;

      if (!component_manager.setup_bot(*bots[bot_index], config,
                                       plugin_manager)) {
        OBCX_KEY_ERROR(common::LogMessageKey::BOT_SETUP_FAILED_TYPE,
                       config.type);
        bots.pop_back();
        continue;
      }

      OBCX_KEY_INFO(common::LogMessageKey::STARTING_BOT, config.type);

      bot_threads.emplace_back([&bots, bot_index]() -> void {
        try {
          bots[bot_index]->run();
        } catch (const std::exception &e) {
          OBCX_KEY_ERROR(common::LogMessageKey::BOT_RUNTIME_ERROR, e.what());
        }
      });
    }

    if (bots.empty()) {
      OBCX_KEY_ERROR(common::LogMessageKey::NO_BOTS_STARTED);
      return 1;
    }

    OBCX_KEY_INFO(common::LogMessageKey::ALL_COMPONENTS_STARTED);

    {
      common::CliHandler::Context ctx{
          .plugin_manager = plugin_manager,
          .config_loader = config_loader,
          .bot_configs = bot_configs,
          .bots = bots,
          .bots_mutex = bots_mutex,
          .should_stop = g_should_stop,
          .stop_cv = g_stop_cv,
      };

      // Shutdown callback runs while the TUI is still alive so the user can
      // see the shutdown log lines in the pane.
      ctx.shutdown_cb = [&]() -> void {
        OBCX_KEY_INFO(common::LogMessageKey::FRAMEWORK_SHUTDOWN);

        for (auto &bot : bots) {
          bot->stop();
        }

        for (size_t i = 0; i < bot_threads.size(); ++i) {
          if (bot_threads[i].joinable()) {
            OBCX_KEY_INFO(common::LogMessageKey::WAITING_BOT_THREAD, i);
            std::atomic_bool thread_finished{false};
            std::thread timeout_thread(
                [&thread_finished, &bot_threads, i]() -> void {
                  std::this_thread::sleep_for(
                      std::chrono::seconds(BOT_SHUTDOWN_TIMEOUT_SECONDS));
                  if (!thread_finished.load()) {
                    OBCX_KEY_WARN(common::LogMessageKey::BOT_THREAD_TIMEOUT, i);
                    bot_threads[i].detach();
                  }
                });

            bot_threads[i].join();
            thread_finished.store(true);

            if (timeout_thread.joinable()) {
              timeout_thread.join();
            }
          }
        }

        // Bots already stopped above, so it's safe to drop the scheduler now.
        if (shared_task_scheduler) {
          shared_task_scheduler->stop();
          shared_task_scheduler.reset();
        }

        plugin_manager.shutdown_all_plugins();

        OBCX_KEY_INFO(common::LogMessageKey::FRAMEWORK_SHUTDOWN_COMPLETE);
      };

      if (use_tui) {
        auto tui_sink = common::Logger::get_tui_sink();
        common::TuiApp tui_app(tui_sink, std::move(ctx));
        tui_app.run();
      } else {
        common::CliHandler cli_handler(ctx);
        cli_handler.run();
        // Block on signal in case stdin closed before SIGINT.
        {
          std::unique_lock lock(g_stop_mtx);
          g_stop_cv.wait(lock, []() -> bool { return g_should_stop.load(); });
        }
        if (ctx.shutdown_cb) {
          ctx.shutdown_cb();
        }
      }
    }

    return 0;
  }
};

} // namespace obcx::common

auto main(int argc, char *argv[]) -> int {
  return obcx::common::MainApplication::run(argc, argv);
}
