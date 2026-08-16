#include "core/command_platform_adapter.hpp"

#include "core/command_matcher.hpp"
#include "interfaces/bot.hpp"
#include "interfaces/telegram_bot.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace obcx::core {
namespace {

auto lowercase(std::string value) -> std::string {
  std::ranges::transform(value, value.begin(), [](const unsigned char byte) {
    if (byte >= 'A' && byte <= 'Z') {
      return static_cast<char>(byte - 'A' + 'a');
    }
    return static_cast<char>(byte);
  });
  return value;
}

auto trim_arguments(std::string_view value) -> std::string {
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.front()))) {
    value.remove_prefix(1);
  }
  return std::string{value};
}

auto raw_text(const MessageEnvelope &event, const std::string_view key)
    -> std::string {
  if (event.raw.is_object() && event.raw.contains(key) &&
      event.raw.at(key).is_string()) {
    return event.raw.at(key).get<std::string>();
  }
  return {};
}

auto command_from_token(std::string token, const std::string_view arguments,
                        const std::string_view bot_target,
                        const bool require_canonical_name)
    -> std::optional<DetectedCommand> {
  if (token.empty() || token.front() != '/') {
    return std::nullopt;
  }
  token.erase(token.begin());
  auto target = std::string{};
  if (const auto separator = token.find('@'); separator != std::string::npos) {
    target = lowercase(token.substr(separator + 1));
    token.resize(separator);
  }
  token = lowercase(std::move(token));
  if (token.empty() || token.size() > command_candidate_max_bytes ||
      (require_canonical_name && !command::valid_name(token))) {
    return std::nullopt;
  }
  if (!target.empty()) {
    auto expected = lowercase(std::string{bot_target});
    if (!expected.empty() && expected.front() == '@') {
      expected.erase(expected.begin());
    }
    if (expected.empty() || target != expected) {
      return std::nullopt;
    }
  }
  return DetectedCommand{
      .name = std::move(token),
      .arguments = trim_arguments(arguments),
  };
}

class TelegramCommandPlatformAdapter final : public ICommandPlatformAdapter {
public:
  [[nodiscard]] auto platform() const noexcept -> std::string_view override {
    return "telegram";
  }

  [[nodiscard]] auto detect(const MessageEnvelope &event,
                            const std::string_view bot_target) const
      -> std::optional<DetectedCommand> override {
    auto text = raw_text(event, "text");
    const common::json *entities = nullptr;
    if (!text.empty() && event.raw.contains("entities")) {
      entities = &event.raw.at("entities");
    } else {
      text = raw_text(event, "caption");
      if (!text.empty() && event.raw.contains("caption_entities")) {
        entities = &event.raw.at("caption_entities");
      }
    }
    if (text.empty() || entities == nullptr || !entities->is_array()) {
      return std::nullopt;
    }
    for (const auto &entity : *entities) {
      if (!entity.is_object() || entity.value("type", "") != "bot_command" ||
          entity.value("offset", -1) != 0) {
        continue;
      }
      const auto length = entity.value("length", 0);
      if (length <= 1 || static_cast<std::size_t>(length) > text.size()) {
        return std::nullopt;
      }
      return command_from_token(
          text.substr(0, static_cast<std::size_t>(length)),
          std::string_view{text}.substr(static_cast<std::size_t>(length)),
          bot_target, true);
    }
    return std::nullopt;
  }

  [[nodiscard]] auto validate_catalog(
      const std::vector<CommandCatalogEntry> &catalog) const
      -> std::optional<std::string> override {
    if (catalog.size() > 100) {
      return "Telegram accepts at most 100 bot commands";
    }
    for (const auto &entry : catalog) {
      if (!command::valid_name(entry.name)) {
        return "Telegram command name is invalid";
      }
      if (entry.description.empty() || entry.description.size() > 256) {
        return "Telegram command description must contain 1 to 256 bytes";
      }
    }
    return std::nullopt;
  }

  [[nodiscard]] auto supports_catalog_publication() const noexcept
      -> bool override {
    return true;
  }

  auto publish_catalog(IBot &bot,
                       const std::vector<CommandCatalogEntry> &catalog)
      -> boost::asio::awaitable<CommandCatalogPublishResult> override {
    auto *telegram = dynamic_cast<ITelegramBot *>(&bot);
    if (telegram == nullptr) {
      co_return CommandCatalogPublishResult{
          .supported = true,
          .succeeded = false,
          .code = "command_catalog_capability_missing",
          .message = "configured Telegram bot lacks ITelegramBot capability",
      };
    }
    std::vector<std::pair<std::string, std::string>> commands;
    commands.reserve(catalog.size());
    for (const auto &entry : catalog) {
      commands.emplace_back(entry.name, entry.description);
    }
    try {
      (void)co_await telegram->set_commands(commands);
      co_return CommandCatalogPublishResult{
          .supported = true,
          .succeeded = true,
      };
    } catch (...) {
      co_return CommandCatalogPublishResult{
          .supported = true,
          .succeeded = false,
          .code = "command_catalog_publish_failed",
          .message = "Telegram command catalog publication failed",
      };
    }
  }
};

class QqCommandPlatformAdapter final : public ICommandPlatformAdapter {
public:
  [[nodiscard]] auto platform() const noexcept -> std::string_view override {
    return "qq";
  }

  [[nodiscard]] auto detect(const MessageEnvelope &event,
                            std::string_view) const
      -> std::optional<DetectedCommand> override {
    const auto text = raw_text(event, "raw_message");
    if (text.empty() || text.front() != '/') {
      return std::nullopt;
    }
    const auto separator = text.find_first_of(" \t\r\n");
    const auto token =
        separator == std::string::npos ? text : text.substr(0, separator);
    const auto arguments = separator == std::string::npos
                               ? std::string_view{}
                               : std::string_view{text}.substr(separator);
    return command_from_token(token, arguments, {}, false);
  }

  [[nodiscard]] auto validate_catalog(
      const std::vector<CommandCatalogEntry> &catalog) const
      -> std::optional<std::string> override {
    for (const auto &entry : catalog) {
      if (!command::valid_name(entry.name) || entry.description.empty()) {
        return "QQ command catalog contains an invalid entry";
      }
    }
    return std::nullopt;
  }

  [[nodiscard]] auto supports_catalog_publication() const noexcept
      -> bool override {
    return false;
  }

  auto publish_catalog(IBot &, const std::vector<CommandCatalogEntry> &)
      -> boost::asio::awaitable<CommandCatalogPublishResult> override {
    co_return CommandCatalogPublishResult{
        .supported = false,
        .succeeded = true,
    };
  }
};

} // namespace

auto command_platform_adapter(const std::string_view platform)
    -> std::shared_ptr<ICommandPlatformAdapter> {
  if (platform == "telegram") {
    return std::make_shared<TelegramCommandPlatformAdapter>();
  }
  if (platform == "qq") {
    return std::make_shared<QqCommandPlatformAdapter>();
  }
  return {};
}

} // namespace obcx::core
