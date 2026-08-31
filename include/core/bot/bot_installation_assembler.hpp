#ifndef OBCX_INCLUDE_CORE_BOT_INSTALLATION_ASSEMBLER_HPP_
#define OBCX_INCLUDE_CORE_BOT_INSTALLATION_ASSEMBLER_HPP_

#include "common/config_loader.hpp"
#include "core/bot/bot_component_runtime.hpp"
#include "core/bot/bot_operation_types.hpp"

#include <memory>
#include <string_view>
#include <vector>

namespace obcx::core {

namespace bot_capability_ids {
inline constexpr std::string_view events = "bot.events";
inline constexpr std::string_view operations = "bot.operations";
inline constexpr std::string_view onebot11_protocol = "onebot11.protocol";
inline constexpr std::string_view onebot11_transport = "onebot11.transport";
inline constexpr std::string_view telegram_protocol = "telegram.protocol";
inline constexpr std::string_view telegram_transport = "telegram.transport";
inline constexpr std::string_view telegram_media_upload =
    "telegram.media-upload";
inline constexpr std::string_view telegram_command_catalog =
    "telegram.command-catalog";
} // namespace bot_capability_ids

struct BotInstallationRecipeDescriptor {
  std::string_view recipe_id;
  common::BotInstallationSurface surface;
  common::BotTransport transport;
  std::vector<ComponentDescriptor> components;
  std::vector<bot::BotAction> advertised_actions;
};

class BotInstallationAssembler {
public:
  [[nodiscard]] static auto describe(
      const common::BotInstallationConfig &config)
      -> BotInstallationRecipeDescriptor;
  [[nodiscard]] static auto validate(
      const common::BotInstallationConfig &config) -> ComponentRecipeValidation;
  [[nodiscard]] static auto assemble(
      const common::BotInstallationConfig &config)
      -> std::unique_ptr<BotInstallation>;
};

} // namespace obcx::core

#endif // OBCX_INCLUDE_CORE_BOT_INSTALLATION_ASSEMBLER_HPP_
