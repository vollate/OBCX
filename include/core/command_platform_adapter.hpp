#ifndef OBCX_INCLUDE_CORE_COMMAND_PLATFORM_ADAPTER_HPP_
#define OBCX_INCLUDE_CORE_COMMAND_PLATFORM_ADAPTER_HPP_

#include "core/actor.hpp"
#include "core/actor_commands.hpp"

#include <boost/asio/awaitable.hpp>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace obcx::core {

class IBot;

struct DetectedCommand {
  std::string name;
  std::string arguments;
};

struct CommandCatalogEntry {
  std::string name;
  std::string description;

  auto operator==(const CommandCatalogEntry &) const -> bool = default;
};

struct CommandCatalogPublishResult {
  bool supported = false;
  bool succeeded = false;
  std::string code;
  std::string message;
};

class ICommandPlatformAdapter {
public:
  ICommandPlatformAdapter() = default;
  ICommandPlatformAdapter(const ICommandPlatformAdapter &) = delete;
  auto operator=(const ICommandPlatformAdapter &)
      -> ICommandPlatformAdapter & = delete;
  ICommandPlatformAdapter(ICommandPlatformAdapter &&) = delete;
  auto operator=(ICommandPlatformAdapter &&)
      -> ICommandPlatformAdapter & = delete;
  virtual ~ICommandPlatformAdapter() = default;

  [[nodiscard]] virtual auto platform() const noexcept -> std::string_view = 0;
  [[nodiscard]] virtual auto detect(const MessageEnvelope &event,
                                    std::string_view bot_target) const
      -> std::optional<DetectedCommand> = 0;
  [[nodiscard]] virtual auto validate_catalog(
      const std::vector<CommandCatalogEntry> &catalog) const
      -> std::optional<std::string> = 0;
  [[nodiscard]] virtual auto supports_catalog_publication() const noexcept
      -> bool = 0;
  virtual auto publish_catalog(IBot &bot,
                               const std::vector<CommandCatalogEntry> &catalog)
      -> boost::asio::awaitable<CommandCatalogPublishResult> = 0;
};

[[nodiscard]] auto command_platform_adapter(std::string_view platform)
    -> std::shared_ptr<ICommandPlatformAdapter>;

} // namespace obcx::core

#endif // OBCX_INCLUDE_CORE_COMMAND_PLATFORM_ADAPTER_HPP_
