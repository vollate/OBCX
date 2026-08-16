#pragma once

#include <boost/asio/awaitable.hpp>
#include <string>
#include <string_view>

namespace obcx::core {

// Stable OneBot/QQ-specific capability surface available to actor packages.
class IQQBot {
public:
  IQQBot() = default;
  IQQBot(const IQQBot &) = delete;
  auto operator=(const IQQBot &) -> IQQBot & = delete;
  IQQBot(IQQBot &&) = delete;
  auto operator=(IQQBot &&) -> IQQBot & = delete;
  virtual ~IQQBot() = default;

  virtual auto get_forward_msg(std::string_view forward_id)
      -> boost::asio::awaitable<std::string> = 0;

  virtual auto get_group_file_url(std::string_view group_id,
                                  std::string_view file_id)
      -> boost::asio::awaitable<std::string> = 0;

  virtual auto get_private_file_url(std::string_view user_id,
                                    std::string_view file_id)
      -> boost::asio::awaitable<std::string> = 0;

  virtual auto group_poke(std::string_view group_id, std::string_view user_id)
      -> boost::asio::awaitable<std::string> = 0;
};

} // namespace obcx::core
