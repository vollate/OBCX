#include "common/component_manager.hpp"
#include "common/logger.hpp"
#include "core/qq_bot.hpp"
#include "core/tg_bot.hpp"
#include "interfaces/bot.hpp"
#include "onebot11/adapter/protocol_adapter.hpp"
#include "telegram/adapter/protocol_adapter.hpp"

#include <cstdint>

namespace {
const uint16_t DEFAULT_PORT = 8080;
}

namespace obcx::common {

auto obcx::common::ComponentManager::instance()
    -> obcx::common::ComponentManager & {
  static ComponentManager instance;
  return instance;
}

auto obcx::common::ComponentManager::create_bot(const BotConfig &config)
    -> std::unique_ptr<::obcx::core::IBot> {
  if (config.type == "qq") {
    return std::make_unique<::obcx::core::QQBot>(
        ::obcx::adapter::onebot11::ProtocolAdapter{});
  }
  if (config.type == "telegram") {
    return std::make_unique<::obcx::core::TGBot>(
        ::obcx::adapter::telegram::ProtocolAdapter{});
  }

  OBCX_ERROR("Unknown bot type: {}", config.type);
  return nullptr;
}

auto ComponentManager::get_connection_type(const std::string &type,
                                           const std::string &bot_type)
    -> ::obcx::network::ConnectionManagerFactory::ConnectionType {
  if (bot_type == "qq") {
    if (type == "websocket" || type == "ws") {
      return ::obcx::network::ConnectionManagerFactory::ConnectionType::
          Onebot11WebSocket;
    }
    if (type == "http") {
      return ::obcx::network::ConnectionManagerFactory::ConnectionType::
          Onebot11HTTP;
    }
  } else if (bot_type == "telegram") {
    if (type == "websocket" || type == "ws") {
      return ::obcx::network::ConnectionManagerFactory::ConnectionType::
          TelegramWebsocket;
    }
    if (type == "http") {
      return ::obcx::network::ConnectionManagerFactory::ConnectionType::
          TelegramHTTP;
    }
  }

  OBCX_ERROR("Unknown connection type: {} for bot type: {}", type, bot_type);
  return ::obcx::network::ConnectionManagerFactory::ConnectionType::
      Onebot11HTTP;
}

auto ComponentManager::create_connection_config(const toml::table &conn_table)
    -> ConnectionConfig {
  ConnectionConfig config;

  if (const auto *host = conn_table.get("host")) {
    config.host = host->value_or<std::string>("localhost");
  } else {
    config.host = "localhost";
  }

  if (const auto *port = conn_table.get("port")) {
    config.port = port->value_or(DEFAULT_PORT);
  } else {
    config.port = DEFAULT_PORT;
  }

  if (const auto *token = conn_table.get("access_token")) {
    config.access_token = token->value_or<std::string>("");
  } else {
    config.access_token = "";
  }

  if (const auto *secret = conn_table.get("secret")) {
    config.secret = secret->value_or<std::string>("");
  } else {
    config.secret = "";
  }

  if (const auto *ssl = conn_table.get("use_ssl")) {
    config.use_ssl = ssl->value_or<bool>(false);
  } else {
    config.use_ssl = false;
  }

  if (const auto *connect_timeout = conn_table.get("connect_timeout")) {
    if (auto connect_timeout_ms = connect_timeout->value<int64_t>()) {
      config.connect_timeout = std::chrono::milliseconds(*connect_timeout_ms);
    }
  }

  if (const auto *action_timeout = conn_table.get("action_timeout")) {
    if (auto action_timeout_ms = action_timeout->value<int64_t>()) {
      config.action_timeout = std::chrono::milliseconds(*action_timeout_ms);
    }
  }

  if (const auto *poll_timeout = conn_table.get("poll_timeout")) {
    if (auto poll_timeout_ms = poll_timeout->value<int64_t>()) {
      config.poll_timeout = std::chrono::milliseconds(*poll_timeout_ms);
    }
  }

  if (const auto *poll_force_close = conn_table.get("poll_force_close")) {
    if (auto poll_force_close_ms = poll_force_close->value<int64_t>()) {
      config.poll_force_close = std::chrono::milliseconds(*poll_force_close_ms);
    }
  }

  if (const auto *poll_retry_interval = conn_table.get("poll_retry_interval")) {
    if (auto poll_retry_interval_ms = poll_retry_interval->value<int64_t>()) {
      config.poll_retry_interval =
          std::chrono::milliseconds(*poll_retry_interval_ms);
    }
  }

  if (const auto *heartbeat_interval = conn_table.get("heartbeat_interval")) {
    if (auto interval_ms = heartbeat_interval->value<int64_t>()) {
      config.heartbeat_interval = std::chrono::milliseconds(*interval_ms);
    }
  }

  if (const auto *proxy_host = conn_table.get("proxy_host")) {
    config.proxy_host = proxy_host->value_or<std::string>("");
  } else {
    config.proxy_host = "";
  }

  if (const auto *proxy_port = conn_table.get("proxy_port")) {
    config.proxy_port = proxy_port->value_or<uint16_t>(0);
  } else {
    config.proxy_port = 0;
  }

  if (const auto *proxy_type = conn_table.get("proxy_type")) {
    config.proxy_type = proxy_type->value_or<std::string>("http");
  } else {
    config.proxy_type = "http";
  }

  OBCX_INFO("Proxy config - Host: '{}', Port: {}, Type: '{}'",
            config.proxy_host, config.proxy_port, config.proxy_type);

  if (const auto *proxy_username = conn_table.get("proxy_username")) {
    config.proxy_username = proxy_username->value_or<std::string>("");
  } else {
    config.proxy_username = "";
  }

  if (const auto *proxy_password = conn_table.get("proxy_password")) {
    config.proxy_password = proxy_password->value_or<std::string>("");
  } else {
    config.proxy_password = "";
  }

  return config;
}

auto ComponentManager::setup_bot(::obcx::core::IBot &bot,
                                 const BotConfig &config) -> bool {
  try {
    auto connection_config = create_connection_config(config.connection);
    std::string conn_type =
        config.connection.get("type")->value_or<std::string>("http");

    bot.connect(get_connection_type(conn_type, config.type), connection_config);

    OBCX_INFO("Bot component setup completed successfully");
    return true;

  } catch (const std::exception &e) {
    OBCX_ERROR("Failed to setup bot component: {}", e.what());
    return false;
  }
}

} // namespace obcx::common
