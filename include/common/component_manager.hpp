#pragma once

#include "common/config_loader.hpp"
#include "interfaces/connection_manager.hpp"

#include <memory>
#include <string>

namespace obcx::core {
class IBot;
}

namespace obcx::common {

class ComponentManager {
public:
  static auto instance() -> ComponentManager &;

  static auto create_bot(const obcx::common::BotConfig &config)
      -> std::unique_ptr<::obcx::core::IBot>;

  static auto get_connection_type(const std::string &type,
                                  const std::string &bot_type)
      -> ::obcx::network::ConnectionManagerFactory::ConnectionType;

  static auto create_connection_config(const toml::table &conn_table)
      -> obcx::common::ConnectionConfig;

  static auto setup_bot(::obcx::core::IBot &bot,
                        const obcx::common::BotConfig &config) -> bool;

private:
  ComponentManager() = default;
};

} // namespace obcx::common
