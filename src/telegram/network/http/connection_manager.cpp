#include "telegram/network/connection_manager.hpp"
#include "common/logger.hpp"
#include "network/proxy_http_client.hpp"
#include "telegram/adapter/protocol_adapter.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <nlohmann/json.hpp>

using obcx::network::ProxyConfig;

namespace obcx::network {

using json = nlohmann::json;

TelegramConnectionManager::TelegramConnectionManager(
    asio::io_context &ioc, adapter::telegram::ProtocolAdapter &adapter)
    : ioc_(ioc), adapter_(adapter), poll_timer_(ioc) {
  OBCX_I18N_INFO(common::LogMessageKey::CONNECTION_ESTABLISHED,
                 "TelegramConnectionManager", "initialized");
}

TelegramConnectionManager::~TelegramConnectionManager() {
  // Release our own resources (poll_timer_, http_client_) while the
  // referenced io_context is still alive. IBot::~IBot guarantees this
  // destruction order.
  disconnect();
  http_client_.reset();
}

void TelegramConnectionManager::connect(
    const common::ConnectionConfig &config) {
  config_ = config;

  if (!config_.proxy_host.empty() && config_.proxy_port > 0) {
    ProxyConfig proxy_config;
    proxy_config.host = config_.proxy_host;
    proxy_config.port = config_.proxy_port;

    if (config_.proxy_type == "socks5") {
      proxy_config.type = ProxyType::SOCKS5;
    } else if (config_.proxy_type == "https") {
      proxy_config.type = ProxyType::HTTPS;
    } else {
      proxy_config.type = ProxyType::HTTP;
    }

    if (!config_.proxy_username.empty()) {
      proxy_config.username = config_.proxy_username;
    }
    if (!config_.proxy_password.empty()) {
      proxy_config.password = config_.proxy_password;
    }

    http_client_ =
        std::make_unique<ProxyHttpClient>(ioc_, proxy_config, config_);
    OBCX_I18N_INFO(common::LogMessageKey::PROXY_CONNECTION_ESTABLISHED,
                   config_.proxy_type, config_.proxy_host, config_.proxy_port,
                   config_.host, config_.port);
  } else {
    http_client_ = std::make_unique<HttpClient>(ioc_, config_);
    OBCX_I18N_INFO(common::LogMessageKey::CONNECTION_ESTABLISHED, config_.host,
                   config_.port);
  }

  is_connected_ = true;
  start_polling();
}

void TelegramConnectionManager::disconnect() {
  stop_polling();
  is_connected_ = false;

  OBCX_I18N_INFO(common::LogMessageKey::CONNECTION_CLOSED);
}

auto TelegramConnectionManager::is_connected() const -> bool {
  return is_connected_.load();
}

auto TelegramConnectionManager::send_action_and_wait_async(
    std::string action_payload, uint64_t echo_id)
    -> asio::awaitable<std::string> {

  if (!http_client_) {
    throw std::runtime_error(common::I18nLogMessages::get_message(
        common::LogMessageKey::HTTP_CLIENT_NOT_INIT));
  }

  try {
    auto payload_json = json::parse(action_payload);
    OBCX_I18N_TRACE(common::LogMessageKey::SENDING_ACTION, action_payload);
    std::string method = payload_json.value("method", "");

    std::map<std::string, std::string> headers;
    headers["Content-Type"] = "application/json";
    headers["User-Agent"] = "OBCX/1.0";

    if (!config_.access_token.empty()) {
      headers["Authorization"] = "Bearer " + config_.access_token;
    }

    std::string api_path = "/bot" + config_.access_token + "/" + method;

    payload_json.erase("method");
    // Telegram Bot API does not understand the OneBot-style `echo` field; strip
    // it before sending so the request is accepted.
    payload_json.erase("echo");
    std::string body = payload_json.dump();

    HttpResponse response =
        co_await http_client_->post(api_path, body, headers);

    if (!response.is_success()) {
      throw std::runtime_error(common::I18nLogMessages::format_message(
          common::LogMessageKey::HTTP_REQUEST_FAILED_STATUS,
          std::to_string(response.status_code)));
    }

    co_return response.body;

  } catch (const std::exception &e) {
    OBCX_I18N_ERROR(common::LogMessageKey::API_REQUEST_FAILED, e.what());
    throw;
  }
}

void TelegramConnectionManager::set_event_callback(EventCallback callback) {
  event_callback_ = std::move(callback);
}

auto TelegramConnectionManager::get_connection_type() const -> std::string {
  return "Telegram_HTTP";
}

auto TelegramConnectionManager::download_file(std::string file_id)
    -> asio::awaitable<std::string> {
  if (!http_client_) {
    throw std::runtime_error(common::I18nLogMessages::get_message(
        common::LogMessageKey::HTTP_CLIENT_NOT_INIT));
  }

  try {
    std::map<std::string, std::string> headers;
    headers["User-Agent"] = "OBCX/1.0";

    if (!config_.access_token.empty()) {
      headers["Authorization"] = "Bearer " + config_.access_token;
    }

    json params = {{"file_id", file_id}};

    std::string get_file_path = "/bot" + config_.access_token + "/getFile";
    std::string body = params.dump();

    headers["Content-Type"] = "application/json";

    HttpResponse response =
        co_await http_client_->post(get_file_path, body, headers);

    if (response.is_success() && !response.body.empty()) {
      auto response_json = json::parse(response.body);

      if (response_json.contains("result") &&
          response_json["result"].contains("file_path")) {
        std::string file_path = response_json["result"]["file_path"];
        // Telegram returns a relative file_path; rebuild the absolute download
        // URL using the bot token.
        std::string download_url = "https://api.telegram.org/file/bot" +
                                   config_.access_token + "/" + file_path;
        co_return download_url;
      } else {
        throw std::runtime_error(common::I18nLogMessages::get_message(
            common::LogMessageKey::TELEGRAM_GETFILE_NO_PATH));
      }
    } else {
      throw std::runtime_error(common::I18nLogMessages::format_message(
          common::LogMessageKey::TELEGRAM_GETFILE_FAILED_STATUS,
          std::to_string(response.status_code)));
    }

  } catch (const std::exception &e) {
    OBCX_I18N_ERROR(common::LogMessageKey::DOWNLOAD_FILE_FAILED, e.what());
    throw;
  }
}

auto TelegramConnectionManager::download_file_content(
    std::string_view download_url) -> asio::awaitable<std::string> {
  if (!http_client_) {
    throw std::runtime_error(common::I18nLogMessages::get_message(
        common::LogMessageKey::HTTP_CLIENT_NOT_INIT));
  }

  try {
    std::string url_str(download_url);
    size_t protocol_pos = url_str.find("://");
    if (protocol_pos == std::string::npos) {
      throw std::runtime_error(common::I18nLogMessages::get_message(
          common::LogMessageKey::DOWNLOAD_URL_INVALID_FORMAT));
    }

    size_t host_start = protocol_pos + 3;
    size_t path_start = url_str.find('/', host_start);
    if (path_start == std::string::npos) {
      throw std::runtime_error(common::I18nLogMessages::get_message(
          common::LogMessageKey::DOWNLOAD_URL_NO_PATH));
    }

    std::string path = url_str.substr(path_start);

    // Empty header map: HttpClient::prepare_request fills in the full
    // browser-like header set we want for the file CDN.
    std::map<std::string, std::string> headers;

    HttpResponse response = co_await http_client_->get(path, headers);

    if (response.is_success()) {
      co_return response.body;
    } else {
      throw std::runtime_error(common::I18nLogMessages::format_message(
          common::LogMessageKey::FILE_DOWNLOAD_FAILED_STATUS,
          std::to_string(response.status_code)));
    }

  } catch (const std::exception &e) {
    OBCX_I18N_ERROR(common::LogMessageKey::DOWNLOAD_FILE_CONTENT_FAILED,
                    e.what());
    throw;
  }
}

auto TelegramConnectionManager::upload_photo_multipart(
    std::string_view chat_id, const std::string &image_data,
    std::string_view filename, std::string_view mime_type,
    std::string_view caption, std::optional<int64_t> message_thread_id)
    -> asio::awaitable<std::string> {
  if (!http_client_) {
    throw std::runtime_error(common::I18nLogMessages::get_message(
        common::LogMessageKey::HTTP_CLIENT_NOT_INIT));
  }

  static constexpr std::string_view kBoundary = "----OBCXBoundary7MA4YWxk";

  std::string body;
  body.reserve(image_data.size() + 512);

  body += "--";
  body += kBoundary;
  body += "\r\nContent-Disposition: form-data; name=\"chat_id\"\r\n\r\n";
  body += chat_id;
  body += "\r\n";

  if (message_thread_id.has_value()) {
    body += "--";
    body += kBoundary;
    body += "\r\nContent-Disposition: form-data; "
            "name=\"message_thread_id\"\r\n\r\n";
    body += std::to_string(*message_thread_id);
    body += "\r\n";
  }

  if (!caption.empty()) {
    body += "--";
    body += kBoundary;
    body += "\r\nContent-Disposition: form-data; name=\"caption\"\r\n\r\n";
    body += caption;
    body += "\r\n";
  }

  body += "--";
  body += kBoundary;
  body += "\r\nContent-Disposition: form-data; name=\"photo\"; filename=\"";
  body += filename;
  body += "\"\r\nContent-Type: ";
  body += mime_type;
  body += "\r\n\r\n";
  body += image_data;
  body += "\r\n";

  body += "--";
  body += kBoundary;
  body += "--\r\n";

  std::map<std::string, std::string> headers;
  headers["Content-Type"] =
      std::string("multipart/form-data; boundary=") + std::string(kBoundary);

  std::string api_path = "/bot" + config_.access_token + "/sendPhoto";
  HttpResponse response = co_await http_client_->post(api_path, body, headers);

  if (!response.is_success()) {
    throw std::runtime_error(common::I18nLogMessages::format_message(
        common::LogMessageKey::HTTP_REQUEST_FAILED_STATUS,
        std::to_string(response.status_code)));
  }

  co_return response.body;
}

void TelegramConnectionManager::start_polling() {
  if (is_polling_.exchange(true) == false) {
    asio::co_spawn(ioc_, poll_updates(), asio::detached);
    OBCX_I18N_INFO(common::LogMessageKey::START_POLLING,
                   config_.poll_timeout.count());
  }
}

void TelegramConnectionManager::stop_polling() {
  is_polling_ = false;
  poll_timer_.cancel();
  OBCX_I18N_INFO(common::LogMessageKey::STOP_POLLING);
}

auto TelegramConnectionManager::poll_updates() -> asio::awaitable<void> {
  while (is_polling_) {
    bool should_delay = false;

    try {
      if (!http_client_) {
        break;
      }

      std::map<std::string, std::string> headers;
      headers["User-Agent"] = "Mozilla/5.0 (X11; Linux x86_64; rv:147.0) "
                              "Gecko/20100101 Firefox/147.0";

      if (!config_.access_token.empty()) {
        headers["Authorization"] = "Bearer " + config_.access_token;
      }

      // poll_timeout: server-side long-poll wait — Telegram blocks up to this
      //   long when there are no new updates.
      // poll_force_close: client-side hard timeout that MUST exceed
      //   poll_timeout, otherwise the client tears down a healthy long-poll
      //   that's just waiting on the server.
      auto poll_timeout_sec =
          std::chrono::duration_cast<std::chrono::seconds>(config_.poll_timeout)
              .count();
      json params = {{"offset", update_offset_},
                     {"limit", 100},
                     {"timeout", poll_timeout_sec}};

      std::string updates_path = "/bot" + config_.access_token + "/getUpdates";
      std::string body = params.dump();

      headers["Content-Type"] = "application/json";

      // Force-close acts as the client safety net: if the connection silently
      // hangs we want to drop and retry rather than waiting forever.
      http_client_->set_timeout(config_.poll_force_close);

      HttpResponse response =
          co_await http_client_->post(updates_path, body, headers);

      if (response.is_success() && !response.body.empty()) {
        process_updates(response.body);
      }

      // Telegram long-poll already implements the wait inside `timeout`; on
      // success we immediately reissue getUpdates with the advanced offset.

    } catch (const std::exception &e) {
      OBCX_I18N_WARN(common::LogMessageKey::POLLING_FAILED, e.what());
      should_delay = true;
    }

    // Only back off when an exception was thrown; happy path loops immediately.
    if (should_delay) {
      poll_timer_.expires_after(config_.poll_retry_interval);
      try {
        co_await poll_timer_.async_wait(asio::use_awaitable);
      } catch (const boost::system::system_error &e) {
        if (e.code() == asio::error::operation_aborted) {
          break;
        }
      }
    }
  }

  OBCX_I18N_DEBUG(common::LogMessageKey::POLLING_COROUTINE_EXIT);
}

void TelegramConnectionManager::process_updates(std::string_view updates_json) {
  try {
    auto json_data = json::parse(updates_json);
    OBCX_I18N_DEBUG(common::LogMessageKey::RECEIVED_UPDATES, updates_json);

    if (json_data.contains("result") && json_data["result"].is_array()) {
      auto result_array = json_data["result"];
      OBCX_I18N_DEBUG(common::LogMessageKey::PROCESSING_UPDATES,
                      result_array.size());

      // Advance offset past the last seen update_id; the next getUpdates call
      // will then ack everything we just processed.
      if (!result_array.empty()) {
        auto last_update = result_array.back();
        if (last_update.contains("update_id")) {
          update_offset_ = last_update["update_id"].get<int>() + 1;
        }
      }

      for (const auto &update_json : result_array) {
        std::string single_update = update_json.dump();
        OBCX_I18N_DEBUG(common::LogMessageKey::PROCESSING_UPDATE,
                        single_update);
        auto event_opt = adapter_.parse_event(single_update);
        if (event_opt && event_callback_) {
          OBCX_I18N_DEBUG(common::LogMessageKey::DISPATCHING_EVENT);
          event_callback_(event_opt.value());
        } else if (!event_opt) {
          OBCX_I18N_DEBUG(common::LogMessageKey::FAILED_PARSE_EVENT);
        } else {
          OBCX_I18N_DEBUG(common::LogMessageKey::EVENT_CALLBACK_NOT_SET);
        }
      }
    }
  } catch (const std::exception &e) {
    OBCX_I18N_WARN(common::LogMessageKey::TELEGRAMBOT_UPDATE_PARSE_ERROR,
                   e.what());
  }
}

} // namespace obcx::network
