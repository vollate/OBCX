#include "telegram/bot/command_adapter.hpp"
#include "core/command/command_detection.hpp"
#include <utility>

namespace obcx::telegram::bot {
namespace {
using core::CommandCatalogEntry;
using core::CommandCatalogPublisher;
using core::CommandCatalogPublishResult;
using core::DetectedCommand;
using core::ICommandPlatformAdapter;
using core::MessageEnvelope;
namespace command = obcx::command;
using core::command_detail::command_from_token;
using core::command_detail::raw_text;
class TelegramCommandPlatformAdapter final : public ICommandPlatformAdapter {
public:
  explicit TelegramCommandPlatformAdapter(std::string bot_target)
      : bot_target_(std::move(bot_target)) {}
  [[nodiscard]] auto platform() const noexcept -> std::string_view override {
    return "telegram";
  }

  [[nodiscard]] auto detect(const MessageEnvelope &event) const
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
          bot_target_, true);
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

  auto publish_catalog(CommandCatalogPublisher *catalog,
                       const std::vector<CommandCatalogEntry> &entries)
      -> boost::asio::awaitable<CommandCatalogPublishResult> override {
    if (catalog == nullptr) {
      co_return CommandCatalogPublishResult{
          .supported = true,
          .succeeded = false,
          .code = "command_catalog_capability_missing",
          .message = "configured Telegram installation lacks command catalog "
                     "capability",
      };
    }
    try {
      co_return co_await catalog->publish(entries);
    } catch (...) {
      co_return CommandCatalogPublishResult{
          .supported = true,
          .succeeded = false,
          .code = "command_catalog_publish_failed",
          .message = "Telegram command catalog publication failed",
      };
    }
  }

private:
  const std::string bot_target_;
};

} // namespace
auto make_command_adapter(std::string bot_target)
    -> std::shared_ptr<core::ICommandPlatformAdapter> {
  return std::make_shared<TelegramCommandPlatformAdapter>(
      std::move(bot_target));
}
} // namespace obcx::telegram::bot
