#pragma once

#include "core/bot_operation_types.hpp"

#include <string_view>

namespace obcx::core {

[[nodiscard]] auto parse_telegram_operation_response(std::string_view response,
                                                     bool side_effecting)
    -> bot::BotOperationResult<bot::Json>;

[[nodiscard]] auto parse_onebot11_operation_response(std::string_view response,
                                                     bool side_effecting)
    -> bot::BotOperationResult<bot::Json>;

} // namespace obcx::core
