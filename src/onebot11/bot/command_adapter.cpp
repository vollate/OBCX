#include "onebot11/bot/command_adapter.hpp"
#include "core/command/command_detection.hpp"
#include <utility>

namespace obcx::onebot11::bot {
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
class QqCommandPlatformAdapter final : public ICommandPlatformAdapter {
public:
  [[nodiscard]] auto platform() const noexcept -> std::string_view override {
    return "qq";
  }

  [[nodiscard]] auto detect(const MessageEnvelope &event) const
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

  auto publish_catalog(CommandCatalogPublisher *,
                       const std::vector<CommandCatalogEntry> &)
      -> boost::asio::awaitable<CommandCatalogPublishResult> override {
    co_return CommandCatalogPublishResult{
        .supported = false,
        .succeeded = true,
    };
  }
};

} // namespace
auto make_command_adapter() -> std::shared_ptr<core::ICommandPlatformAdapter> {
  return std::make_shared<QqCommandPlatformAdapter>();
}
} // namespace obcx::onebot11::bot
