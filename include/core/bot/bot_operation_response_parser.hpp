#ifndef OBCX_INCLUDE_CORE_BOT_OPERATION_RESPONSE_PARSER_HPP_
#define OBCX_INCLUDE_CORE_BOT_OPERATION_RESPONSE_PARSER_HPP_

#include "core/bot/bot_operation_types.hpp"

#include <string_view>

namespace obcx::core {

[[nodiscard]] auto parse_telegram_operation_response(std::string_view response,
                                                     bool side_effecting)
    -> bot::BotOperationResult<bot::Json>;

[[nodiscard]] auto parse_onebot11_operation_response(std::string_view response,
                                                     bool side_effecting)
    -> bot::BotOperationResult<bot::Json>;

} // namespace obcx::core

#endif // OBCX_INCLUDE_CORE_BOT_OPERATION_RESPONSE_PARSER_HPP_
