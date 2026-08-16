#pragma once

#include "interfaces/bot.hpp"

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace obcx::core {

struct RegisteredBot {
  std::string platform;
  std::string bot_id;
  std::shared_ptr<IBot> bot;
};

class BotRegistry {
public:
  void register_bot(std::string platform, std::string bot_id,
                    const std::shared_ptr<IBot> &bot) {
    if (!bot) {
      return;
    }
    std::scoped_lock lock(mutex_);
    const auto platform_key = platform;
    const auto bot_key = bot_id;
    bots_[platform_key][bot_key] = BotEntry{
        .platform = std::move(platform),
        .bot_id = std::move(bot_id),
        .bot = bot,
    };
  }

  void unregister_bot(std::string_view platform, std::string_view bot_id) {
    std::scoped_lock lock(mutex_);
    const auto platform_it = bots_.find(std::string{platform});
    if (platform_it == bots_.end()) {
      return;
    }
    platform_it->second.erase(std::string{bot_id});
    if (platform_it->second.empty()) {
      bots_.erase(platform_it);
    }
  }

  [[nodiscard]] auto find_bot(std::string_view platform,
                              std::string_view bot_id) const
      -> std::optional<RegisteredBot> {
    std::scoped_lock lock(mutex_);
    const auto platform_it = bots_.find(std::string{platform});
    if (platform_it == bots_.end()) {
      return std::nullopt;
    }
    const auto bot_it = platform_it->second.find(std::string{bot_id});
    if (bot_it == platform_it->second.end()) {
      return std::nullopt;
    }
    auto bot = bot_it->second.bot.lock();
    if (!bot) {
      return std::nullopt;
    }
    return RegisteredBot{.platform = bot_it->second.platform,
                         .bot_id = bot_it->second.bot_id,
                         .bot = std::move(bot)};
  }

  // Platform-only lookup is retained for actors configured with one account.
  // Returning no result for an ambiguous platform prevents traffic from being
  // silently sent through whichever account happened to register last.
  [[nodiscard]] auto find_bot(std::string_view platform) const
      -> std::optional<RegisteredBot> {
    std::scoped_lock lock(mutex_);
    const auto it = bots_.find(std::string{platform});
    if (it == bots_.end()) {
      return std::nullopt;
    }
    std::optional<RegisteredBot> live;
    for (const auto &[bot_id, entry] : it->second) {
      (void)bot_id;
      auto bot = entry.bot.lock();
      if (!bot) {
        continue;
      }
      if (live) {
        return std::nullopt;
      }
      live = RegisteredBot{.platform = entry.platform,
                           .bot_id = entry.bot_id,
                           .bot = std::move(bot)};
    }
    return live;
  }

private:
  struct BotEntry {
    std::string platform;
    std::string bot_id;
    std::weak_ptr<IBot> bot;
  };
  using PlatformBots = std::unordered_map<std::string, BotEntry>;

  mutable std::mutex mutex_;
  std::unordered_map<std::string, PlatformBots> bots_;
};

} // namespace obcx::core
