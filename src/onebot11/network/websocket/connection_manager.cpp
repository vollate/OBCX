#include "onebot11/network/websocket/connection_manager.hpp"
#include "common/logger.hpp"
#include "onebot11/adapter/protocol_adapter.hpp"

#include <atomic>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/bind/bind.hpp>
#include <utility>

namespace obcx::network {

WebSocketConnectionManager::WebSocketConnectionManager(
    asio::io_context &ioc, adapter::onebot11::ProtocolAdapter &adapter)
    : ioc_(ioc), adapter_(adapter), reconnect_timer_(ioc),
      send_strand_(asio::make_strand(ioc)) {}

WebSocketConnectionManager::~WebSocketConnectionManager() {
  // Release our own resources while the referenced io_context is still alive
  // (IBot::~IBot guarantees this destruction order). Without this explicit
  // cleanup, send_strand_ — which holds a shared_ptr into the io_context's
  // strand_executor_service — would be destroyed after the service was gone,
  // crashing inside _Sp_counted_ptr_inplace::_M_destroy.
  disconnect();
  // disconnect() posts a detached close() but doesn't drop the client.
  // Reset it here so its socket/stream destructors run against a live
  // executor service.
  ws_client_.reset();
}

void WebSocketConnectionManager::set_event_callback(EventCallback callback) {
  event_callback_ = std::move(callback);
}

void WebSocketConnectionManager::connect(
    const common::ConnectionConfig &config) {
  // Per-API-request timeout (for action echo responses) is distinct from the
  // TCP connect timeout. Using connect_timeout (5s default) here previously
  // caused duplicate QQ deliveries: first-time media sends can take ~8s for
  // llonebot to ack, which tripped the timeout, fired a retry, and then the
  // original response also arrived successfully on the server.
  action_timeout_ = config.action_timeout;
  connect_ws(config.host, config.port, config.access_token);
}

void WebSocketConnectionManager::disconnect() {
  is_running_ = false;
  is_connected_.store(false, std::memory_order_release);

  // Drain pending requests so their completion handlers don't fire after we
  // tear down strands/io_context.
  {
    std::scoped_lock lock(pending_requests_mutex_);
    for (auto &[echo_id, request] : pending_requests_) {
      if (request) {
        request->timeout_timer.cancel();
        if (request->completion_handler) {
          try {
            request->completion_handler(asio::error::operation_aborted, "");
          } catch (...) {
            // FIXME: Log the exception
          }
        }
      }
    }
    pending_requests_.clear();
    OBCX_I18N_DEBUG(common::LogMessageKey::ONEBOT11_WS_PENDING_CLEARED);
  }

  if (ws_client_) {
    asio::co_spawn(ioc_, ws_client_->close(), asio::detached);
  }

  reconnect_timer_.cancel();
}

auto WebSocketConnectionManager::get_connection_type() const -> std::string {
  return "WebSocket";
}

void WebSocketConnectionManager::connect_ws(std::string host, uint16_t port,
                                            std::string access_token) {
  if (is_running_) {
    OBCX_I18N_WARN(common::LogMessageKey::ONEBOT11_WS_ALREADY_RUNNING);
    return;
  }
  host_ = std::move(host);
  port_ = port;
  access_token_ = std::move(access_token);
  is_running_ = true;

  do_connect();
}

void WebSocketConnectionManager::do_connect() {
  asio::post(send_strand_, [this]() -> void {
    ws_client_ = std::make_shared<WebsocketClient>(ioc_);
    OBCX_I18N_INFO(common::LogMessageKey::WEBSOCKET_ATTEMPTING_CONNECTION,
                   host_, port_);

    asio::co_spawn(send_strand_,
                   ws_client_->run(host_, std::to_string(port_), access_token_,
                                   [this](const beast::error_code &ec,
                                          const std::string &message) -> void {
                                     this->on_ws_message(ec, message);
                                   }),
                   asio::detached);
  });
}

void WebSocketConnectionManager::on_ws_message(const beast::error_code &ec,
                                               const std::string &message) {
  OBCX_I18N_TRACE(common::LogMessageKey::ONEBOT11_WS_MSG_RECEIVED, message);
  if (ec) {
    OBCX_I18N_ERROR(common::LogMessageKey::ONEBOT11_WS_DISCONNECTED_ERROR,
                    ec.message());
    {
      is_connected_.store(false, std::memory_order_release);
    }
    schedule_reconnect();
    return;
  }

  if (message.empty()) {
    OBCX_I18N_INFO(common::LogMessageKey::WEBSOCKET_CONNECTION_ESTABLISHED);
    {
      is_connected_.store(true, std::memory_order_release);
    }
    reconnect_timer_.cancel();
    return;
  }

  OBCX_I18N_TRACE(common::LogMessageKey::ONEBOT11_WS_RAW_MSG, message);

  try {
    nlohmann::json j = nlohmann::json::parse(message);

    if (j.contains("echo") && j.contains("retcode")) {
      uint64_t echo = j["echo"];

      std::scoped_lock lock(pending_requests_mutex_);
      OBCX_I18N_DEBUG(common::LogMessageKey::ONEBOT11_WS_FIND_PENDING, echo,
                      pending_requests_.size());
      auto it = pending_requests_.find(echo);
      if (it != pending_requests_.end()) {
        auto request = it->second;
        pending_requests_.erase(it);

        request->need_wait.store(false, std::memory_order_release);
        request->timeout_timer.cancel();

        if (request->completion_handler) {
          OBCX_I18N_DEBUG(common::LogMessageKey::ONEBOT11_WS_CALL_COMPLETION,
                          echo);
          request->completion_handler(boost::system::error_code{}, message);
        } else {
          OBCX_I18N_ERROR(common::LogMessageKey::ONEBOT11_WS_COMPLETION_NULL,
                          echo);
        }
        OBCX_I18N_DEBUG(common::LogMessageKey::ONEBOT11_WS_API_RESPONSE_HANDLED,
                        echo);
        return;
      }
      OBCX_I18N_WARN(common::LogMessageKey::ONEBOT11_WS_UNKNOWN_API_RESPONSE,
                     echo);
      // Dump all pending echo IDs to help diagnose unmatched-echo bugs.
#ifdef __OBCX_DEBUG_COMPILATION
      std::stringstream pending_echos;
      bool first = true;
      for (const auto &[id, req] : pending_requests_) {
        if (!first) {
          pending_echos << ", ";
        }
        first = false;
        pending_echos << id;
      }
      OBCX_I18N_DEBUG(common::LogMessageKey::ONEBOT11_WS_CURRENT_PENDING,
                      pending_echos.str());
#endif
    }
  } catch (const nlohmann::json::exception &e) {
    OBCX_I18N_WARN(common::LogMessageKey::ONEBOT11_WS_JSON_PARSE_FAILED,
                   e.what());
  }

  auto event_opt = adapter_.parse_event(message);
  if (event_opt) {
    if (event_callback_) {
      event_callback_(event_opt.value());
    }
  } else {
    OBCX_I18N_DEBUG(common::LogMessageKey::ONEBOT11_WS_INVALID_EVENT);
  }
}

void WebSocketConnectionManager::schedule_reconnect() {
  reconnect_timer_.expires_after(std::chrono::seconds(5));
  OBCX_I18N_INFO(common::LogMessageKey::ONEBOT11_WS_RECONNECT_SCHEDULED, 5000);
  reconnect_timer_.async_wait([this](const beast::error_code &ec) -> void {
    if (ec) {
      if (ec != asio::error::operation_aborted) {
        OBCX_I18N_ERROR(
            common::LogMessageKey::ONEBOT11_WS_RECONNECT_TIMER_ERROR,
            ec.message());
      }
      return;
    }
    do_connect();
  });
}

auto WebSocketConnectionManager::send_action_and_wait_async(
    std::string action_payload, uint64_t echo_id)
    -> asio::awaitable<std::string> {
  if (!ws_client_) {
    throw std::runtime_error(common::I18nLogMessages::get_message(
        common::LogMessageKey::ONEBOT11_WS_NO_CLIENT));
  }

  OBCX_I18N_DEBUG(common::LogMessageKey::ONEBOT11_WS_USE_COROUTINE);

  std::optional<std::string> response_result;
  std::optional<boost::system::error_code> response_error;
  std::mutex result_mutex;

  auto request = std::make_shared<PendingRequest>(ioc_);

  request->completion_handler =
      [&result_mutex, &response_result, &response_error,
       request](boost::system::error_code ec, std::string response) -> void {
    std::scoped_lock lock(result_mutex);
    if (ec) {
      response_error = ec;
    } else {
      response_result = std::move(response);
    }
    request->timeout_timer.cancel();
  };

  request->timeout_timer.expires_after(action_timeout_);

  {
    std::scoped_lock lock(pending_requests_mutex_);
    pending_requests_[echo_id] = request;
    OBCX_I18N_DEBUG(common::LogMessageKey::ONEBOT11_WS_ADD_PENDING_COROUTINE,
                    echo_id, pending_requests_.size());
  }

  try {
    co_await asio::co_spawn(
        send_strand_,
        [this, action_payload =
                   std::move(action_payload)]() -> asio::awaitable<void> {
          co_await ws_client_->send(action_payload);
        },
        asio::use_awaitable);

    OBCX_I18N_DEBUG(common::LogMessageKey::ONEBOT11_WS_MSG_SENT_COROUTINE,
                    echo_id);

    if (request->need_wait.load(std::memory_order_acquire)) {
      try {
        co_await request->timeout_timer.async_wait(asio::use_awaitable);
        // Reaching here means the timer expired without being cancelled by an
        // incoming response, i.e. the API call genuinely timed out.
        OBCX_I18N_DEBUG(common::LogMessageKey::ONEBOT11_WS_REQUEST_TIMEOUT,
                        echo_id);
        response_error = asio::error::timed_out;
      } catch (const boost::system::system_error &e) {
        if (e.code() == asio::error::operation_aborted) {
          // Timer cancellation is the normal path: the matching echo response
          // arrived and on_ws_message cancelled the timer.
          OBCX_I18N_DEBUG(
              common::LogMessageKey::ONEBOT11_WS_RESPONSE_RECEIVED_TIMER,
              echo_id);
        } else {
          throw;
        }
      }
    }

    {
      std::scoped_lock lock(pending_requests_mutex_);
      pending_requests_.erase(echo_id);
      OBCX_I18N_DEBUG(
          common::LogMessageKey::ONEBOT11_WS_CLEAN_PENDING_COROUTINE, echo_id,
          pending_requests_.size());
    }

    {
      std::scoped_lock lock(result_mutex);
      if (response_error) {
        if (response_error == asio::error::timed_out) {
          OBCX_I18N_ERROR(
              common::LogMessageKey::ONEBOT11_WS_API_TIMEOUT_COROUTINE,
              echo_id);
          throw std::runtime_error(common::I18nLogMessages::get_message(
              common::LogMessageKey::ONEBOT11_WS_API_TIMEOUT_MSG));
        }
        throw boost::system::system_error(*response_error);
      }

      if (response_result) {
        OBCX_I18N_DEBUG(
            common::LogMessageKey::ONEBOT11_WS_API_SUCCESS_COROUTINE, echo_id,
            response_result->length());
        co_return *response_result;
      }

      throw std::runtime_error(common::I18nLogMessages::get_message(
          common::LogMessageKey::ONEBOT11_WS_UNKNOWN_ERROR));
    }

  } catch (...) {
    request->timeout_timer.cancel();
    {
      std::scoped_lock lock(pending_requests_mutex_);
      pending_requests_.erase(echo_id);
    }
    throw;
  }
}

auto WebSocketConnectionManager::is_connected() const -> bool {
  return is_connected_.load(std::memory_order_acquire);
}

void WebSocketConnectionManager::handle_timeout(uint64_t echo_id) {
  std::scoped_lock lock(pending_requests_mutex_);
  auto it = pending_requests_.find(echo_id);
  if (it != pending_requests_.end()) {
    auto request = it->second;
    pending_requests_.erase(it);

    if (request->completion_handler) {
      request->completion_handler(asio::error::timed_out, "");
    }
  }
}

} // namespace obcx::network
