#pragma once

#include "core/bot_operation_dispatcher.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace obcx::core {

class IBot;

[[nodiscard]] auto bot_surface_for_config_type(std::string_view type)
    -> bot::BotSurface;

[[nodiscard]] auto make_existing_bot_operation_endpoint(
    std::string installation_id, std::string_view configured_type,
    std::shared_ptr<IBot> live_bot) -> std::shared_ptr<BotOperationEndpoint>;

void register_existing_bot_operation_endpoint(
    QQTelegramOperationDispatcher &dispatcher, std::string installation_id,
    std::string_view configured_type, std::shared_ptr<IBot> live_bot);

} // namespace obcx::core
