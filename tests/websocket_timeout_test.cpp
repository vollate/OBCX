#include <atomic>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <chrono>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <thread>

#include "common/logger.hpp"
#include "common/message_type.hpp"
#include "onebot11/adapter/protocol_adapter.hpp"
#include "onebot11/network/websocket/connection_manager.hpp"
// NOLINTBEGIN

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace asio = boost::asio;
using tcp = asio::ip::tcp;

namespace obcx::test {

constexpr size_t SERVER_STARTUP_DELAY = 1000;
constexpr size_t CONNECTION_ESTABLISH_DELAY = 200;
constexpr size_t NORMAL_RESPONSE_DELAY = 100;
constexpr size_t DELAYED_RESPONSE_TIME = 3000;

// Default client-side timeout. The TimeoutScenario test infers this is 30s
// in production; we use a tighter value here so the test runs quickly.
constexpr std::chrono::seconds CLIENT_DEFAULT_TIMEOUT{5};
// Wait long enough for the client default timeout to fire, plus a margin.
constexpr std::chrono::seconds EXTENDED_TIMEOUT{CLIENT_DEFAULT_TIMEOUT +
                                                std::chrono::seconds(5)};
// For the delayed-response test: must exceed DELAYED_RESPONSE_TIME but stay
// below the client default timeout.
constexpr std::chrono::seconds DELAYED_WAIT_TIMEOUT{10};
constexpr uint64_t TEST_ECHO_1 = 12345;
constexpr uint64_t TEST_ECHO_2 = 54321;
constexpr uint64_t TEST_ECHO_3 = 67890;

/**
 * Mock WebSocket server for these tests.
 * (Self-contained: owns its thread and io_context.)
 */
class MockWebSocketServer {
public:
  explicit MockWebSocketServer(const std::string &host)
      : ioc_(), endpoint_(asio::ip::make_address(host), 0), acceptor_(ioc_),
        work_guard_(asio::make_work_guard(ioc_)) {
    acceptor_.open(endpoint_.protocol());
    acceptor_.set_option(asio::socket_base::reuse_address(true));
    acceptor_.bind(endpoint_);
    acceptor_.listen();
    endpoint_ = acceptor_.local_endpoint();
  }

  ~MockWebSocketServer() {
    if (thread_.joinable()) {
      join_and_stop();
    }
  }

  void start() {
    thread_ = std::thread([this]() {
      OBCX_DEBUG("Server thread started on {}:{}",
                 endpoint_.address().to_string(), endpoint_.port());
      do_accept();
      ioc_.run();
      OBCX_DEBUG("Server thread stopped");
    });
  }

  void join_and_stop() {
    asio::post(ioc_, [this]() {
      OBCX_DEBUG("Stopping server...");
      acceptor_.close();
      if (ws_ && ws_->is_open()) {
        ws_->async_close(websocket::close_code::normal,
                         [this](const boost::system::error_code &ec) {
                           work_guard_.reset();
                           ioc_.stop();
                         });
      } else {
        work_guard_.reset();
        ioc_.stop();
      }
    });

    if (thread_.joinable()) {
      thread_.join();
    }
  }

  void set_response_delay(size_t delay) { response_delay_ = delay; }

  void set_should_respond(bool should_respond) {
    should_respond_ = should_respond;
  }

  [[nodiscard]] auto get_port() const -> uint16_t { return endpoint_.port(); }

private:
  void do_accept() {
    acceptor_.async_accept([this](beast::error_code ec, tcp::socket socket) {
      if (!acceptor_.is_open()) {
        return; // server is shutting down
      }
      if (!ec) {
        OBCX_DEBUG("Accepted new connection");
        handle_websocket(std::move(socket));
      }
      do_accept(); // keep accepting
    });
  }

  void handle_websocket(tcp::socket socket) {
    ws_ = std::make_shared<websocket::stream<tcp::socket>>(std::move(socket));

    ws_->async_accept([this, ws = ws_](beast::error_code ec) {
      if (!ec) {
        start_read_loop(ws);
      }
    });
  }

  void start_read_loop(
      const std::shared_ptr<websocket::stream<tcp::socket>> &ws) {
    auto buffer = std::make_shared<beast::flat_buffer>();

    ws->async_read(
        *buffer, [this, ws, buffer](beast::error_code ec,
                                    std::size_t /*bytes_transferred*/) {
          if (ec == websocket::error::closed ||
              ec == asio::error::operation_aborted) {
            return; // connection closed
          }
          if (!ec) {
            std::string message = beast::buffers_to_string(buffer->data());
            OBCX_DEBUG("Received message: {}", message);
            handle_message(ws, message);
            start_read_loop(ws); // keep reading
          }
        });
  }

  void handle_message(const std::shared_ptr<websocket::stream<tcp::socket>> &ws,
                      const std::string &message) {
    if (!should_respond_) {
      OBCX_DEBUG("Configured not to respond");
      return;
    }

    try {
      nlohmann::json request = nlohmann::json::parse(message);
      uint64_t echo = request.value("echo", 0);

      nlohmann::json response;
      response["retcode"] = 0;
      response["status"] = "ok";
      response["data"] = {};
      response["echo"] = echo;

      std::string response_str = response.dump();

      if (response_delay_.load() > 0) {
        auto timer = std::make_shared<asio::steady_timer>(ioc_);
        timer->expires_after(std::chrono::milliseconds(response_delay_));
        timer->async_wait(
            [this, ws, response_str, timer, echo](beast::error_code ec) {
              if (!ec) {
                OBCX_DEBUG("Sending response after {}ms delay (echo: {})",
                           response_delay_.load(), echo);
                ws->async_write(asio::buffer(response_str), asio::detached);
              }
            });
      } else {
        OBCX_DEBUG("Sending response immediately (echo: {})", echo);
        ws->async_write(asio::buffer(response_str), asio::detached);
      }
    } catch (const nlohmann::json::parse_error &e) {
      OBCX_ERROR("JSON parse error: {}", e.what());
    }
  }

  asio::io_context ioc_;
  tcp::endpoint endpoint_;
  tcp::acceptor acceptor_;
  asio::executor_work_guard<asio::io_context::executor_type> work_guard_;
  std::thread thread_;

  std::shared_ptr<websocket::stream<tcp::socket>> ws_;
  std::atomic<size_t> response_delay_{0};
  std::atomic<bool> should_respond_{true};
};

/**
 * Timeout-mechanism test fixture.
 */
class WsTimeoutTest : public testing::Test {

protected:
  void SetUp() override {
    common::Logger::initialize(spdlog::level::trace);
    server_ = std::make_unique<MockWebSocketServer>("127.0.0.1");
    server_->start();

    std::this_thread::sleep_for(
        std::chrono::milliseconds(SERVER_STARTUP_DELAY));

    adapter_ = std::make_unique<adapter::onebot11::ProtocolAdapter>();
    connection_manager_ = std::make_unique<network::WebSocketConnectionManager>(
        client_ioc_, *adapter_);
  }

  void TearDown() override {
    if (connection_manager_) {
      connection_manager_->disconnect();
    }
    work_guard_.reset();
    client_ioc_.stop();
    if (client_thread_.joinable()) {
      client_thread_.join();
    }
  }

  void start_client_ioc() {
    client_thread_ = std::thread([this]() {
      work_guard_.emplace(client_ioc_.get_executor());
      client_ioc_.run();
    });
  }

  void connect_to_server() {
    common::ConnectionConfig config;
    config.host = "127.0.0.1";
    config.port = server_->get_port();
    config.access_token = "test_token";

    connection_manager_->connect(config);
    while (!connection_manager_->is_connected()) {
      std::this_thread::sleep_for(
          std::chrono::milliseconds(CONNECTION_ESTABLISH_DELAY));
    }
  }

  asio::io_context client_ioc_;
  std::unique_ptr<MockWebSocketServer> server_;
  std::unique_ptr<adapter::onebot11::ProtocolAdapter> adapter_;
  std::unique_ptr<network::WebSocketConnectionManager> connection_manager_;
  std::thread client_thread_;
  std::optional<asio::executor_work_guard<asio::io_context::executor_type>>
      work_guard_;
};

/**
 * Normal response: server replies quickly, request resolves successfully.
 */
TEST_F(WsTimeoutTest, NormalResponse) {
  start_client_ioc();
  connect_to_server();

  server_->set_should_respond(true);
  server_->set_response_delay(NORMAL_RESPONSE_DELAY);

  nlohmann::json request;
  request["action"] = "get_login_info";
  request["echo"] = TEST_ECHO_1;

  auto start_time = std::chrono::steady_clock::now();

  std::promise<std::string> result_promise;
  auto result_future = result_promise.get_future();

  asio::co_spawn(
      client_ioc_,
      [this, request, &result_promise]() -> asio::awaitable<void> {
        try {
          OBCX_INFO("Start sending");
          auto result =
              co_await connection_manager_->send_action_and_wait_async(
                  request.dump(), TEST_ECHO_1);
          result_promise.set_value(result);
        } catch (const std::exception &e) {
          result_promise.set_exception(std::current_exception());
        }
      },
      asio::detached);

  // Wait with margin over the expected response time.
  auto status = result_future.wait_for(std::chrono::seconds(3));
  auto end_time = std::chrono::steady_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time);

  ASSERT_EQ(status, std::future_status::ready)
      << "request should complete within 2 seconds";

  std::string response;
  ASSERT_NO_THROW(response = result_future.get())
      << "request should complete without throwing";

  ASSERT_FALSE(response.empty()) << "response should not be empty";

  auto response_json = nlohmann::json::parse(response);
  EXPECT_EQ(response_json["echo"], TEST_ECHO_1) << "echo should match";
  EXPECT_EQ(response_json["retcode"], 0) << "retcode should be 0";

  EXPECT_LT(duration.count(), 1000) << "response time should be under 1 second";
  EXPECT_GT(duration.count(), NORMAL_RESPONSE_DELAY - 50)
      << "response time should be slightly greater than the server delay";
}

/**
 * Timeout: server never replies; client should give up after its timeout.
 */
TEST_F(WsTimeoutTest, TimeoutScenario) {
  start_client_ioc();
  connect_to_server();

  server_->set_should_respond(false);

  nlohmann::json request;
  request["action"] = "get_login_info";
  request["echo"] = TEST_ECHO_2;

  auto start_time = std::chrono::steady_clock::now();

  auto result_promise = std::make_shared<std::promise<void>>();
  auto result_future = result_promise->get_future();
  auto timeout_occurred = std::make_shared<std::atomic<bool>>(false);

  asio::co_spawn(
      client_ioc_,
      [this, request, result_promise,
       timeout_occurred]() -> asio::awaitable<void> {
        try {
          [[maybe_unused]] std::string _ =
              co_await connection_manager_->send_action_and_wait_async(
                  request.dump(), TEST_ECHO_2);
          result_promise->set_value(); // should not be reached
        } catch (const std::runtime_error &e) {
          std::string error_msg = e.what();
          // Timeout error message is expected to contain "timeout".
          if (error_msg.find("timeout") != std::string::npos) {
            *timeout_occurred = true;
          }
          result_promise->set_exception(std::current_exception());
        } catch (...) {
          result_promise->set_exception(std::current_exception());
        }
      },
      asio::detached);

  // Wait slightly longer than the client's internal timeout.
  auto status = result_future.wait_for(EXTENDED_TIMEOUT);
  auto end_time = std::chrono::steady_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);

  ASSERT_EQ(status, std::future_status::ready)
      << "coroutine should finish (via timeout) within " << EXTENDED_TIMEOUT.count()
      << " seconds";

  EXPECT_THROW(result_future.get(), std::runtime_error)
      << "should throw std::runtime_error";
  EXPECT_TRUE(*timeout_occurred)
      << "exception message should indicate a timeout";

  // Verify the elapsed timeout is within the expected window.
  EXPECT_GE(duration.count(), CLIENT_DEFAULT_TIMEOUT.count() - 2)
      << "timeout duration should be close to " << CLIENT_DEFAULT_TIMEOUT.count()
      << " seconds";
  EXPECT_LE(duration.count(), CLIENT_DEFAULT_TIMEOUT.count() + 2)
      << "timeout duration should be close to " << CLIENT_DEFAULT_TIMEOUT.count()
      << " seconds";
}

/**
 * Delayed response: server replies just before the client's timeout fires.
 */
TEST_F(WsTimeoutTest, DelayedResponse) {
  start_client_ioc();
  connect_to_server();

  server_->set_should_respond(true);
  server_->set_response_delay(DELAYED_RESPONSE_TIME);

  nlohmann::json request;
  request["action"] = "get_login_info";
  request["echo"] = TEST_ECHO_3;

  auto start_time = std::chrono::steady_clock::now();

  std::promise<std::string> result_promise;
  auto result_future = result_promise.get_future();

  asio::co_spawn(
      client_ioc_,
      [this, request, &result_promise]() -> asio::awaitable<void> {
        try {
          auto result =
              co_await connection_manager_->send_action_and_wait_async(
                  request.dump(), TEST_ECHO_3);
          result_promise.set_value(result);
        } catch (const std::exception &e) {
          result_promise.set_exception(std::current_exception());
        }
      },
      asio::detached);

  auto status = result_future.wait_for(DELAYED_WAIT_TIMEOUT);
  auto end_time = std::chrono::steady_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time);

  ASSERT_EQ(status, std::future_status::ready)
      << "request should complete within " << DELAYED_WAIT_TIMEOUT.count()
      << " seconds";

  std::string response;
  ASSERT_NO_THROW(response = result_future.get())
      << "delayed request should complete successfully";

  auto response_json = nlohmann::json::parse(response);
  EXPECT_EQ(response_json["echo"], TEST_ECHO_3) << "echo should match";

  EXPECT_GE(duration.count(), DELAYED_RESPONSE_TIME - 200)
      << "response time should be slightly greater than " << DELAYED_RESPONSE_TIME
      << " ms";
  EXPECT_LE(duration.count(), DELAYED_RESPONSE_TIME + 500)
      << "response time should be approximately " << DELAYED_RESPONSE_TIME
      << " ms";
}

} // namespace obcx::test
// NOLINTEND
