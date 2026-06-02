/**
 * @file websocket_queue_test.cpp
 * @brief Tests for the WebSocket write-queue mechanism.
 *
 * Verifies that under weak-network conditions the WebSocket client's write
 * queue serialises concurrent send requests correctly, avoiding Beast's
 * internal "concurrent write" race.
 */

#include "common/logger.hpp"
#include "network/websocket_client.hpp"

#include <atomic>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <queue>
#include <thread>

// NOLINTBEGIN

namespace beast = boost::beast;
namespace asio = boost::asio;
using tcp = asio::ip::tcp;

namespace obcx::test {

constexpr size_t SERVER_STARTUP_DELAY = 1000;
constexpr size_t CONNECTION_ESTABLISH_DELAY = 500;
constexpr size_t CONCURRENT_WRITE_COUNT = 10;
constexpr size_t WEAK_NETWORK_WRITE_COUNT = 20;
constexpr size_t WEAK_NETWORK_DELAY_MS = 100;

/**
 * Mock WebSocket server that simulates a weak-network environment.
 */
class MockWebSocketServer {
public:
  explicit MockWebSocketServer(const std::string &host)
      : ioc_(), endpoint_(asio::ip::make_address(host), 0), acceptor_(ioc_),
        work_guard_(asio::make_work_guard(ioc_)), accepting_(true) {
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
    accepting_ = false;
    asio::post(ioc_, [this]() {
      OBCX_DEBUG("Stopping server...");
      acceptor_.close();
      if (ws_ && ws_->is_open()) {
        ws_->async_close(beast::websocket::close_code::normal,
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

  [[nodiscard]] auto get_port() const -> uint16_t { return endpoint_.port(); }

  [[nodiscard]] auto get_received_count() const -> size_t {
    return received_count_.load();
  }

private:
  void do_accept() {
    acceptor_.async_accept([this](beast::error_code ec, tcp::socket socket) {
      if (!acceptor_.is_open() || !accepting_) {
        return;
      }
      if (!ec) {
        OBCX_DEBUG("Accepted new connection");
        handle_websocket(std::move(socket));
      }
      do_accept();
    });
  }

  void handle_websocket(tcp::socket socket) {
    ws_ = std::make_shared<beast::websocket::stream<tcp::socket>>(
        std::move(socket));

    ws_->async_accept([this, ws = ws_](beast::error_code ec) {
      if (!ec) {
        start_read_loop(ws);
      }
    });
  }

  void start_read_loop(
      const std::shared_ptr<beast::websocket::stream<tcp::socket>> &ws) {
    auto buffer = std::make_shared<beast::flat_buffer>();

    ws->async_read(*buffer, [this, ws,
                             buffer](beast::error_code ec,
                                     std::size_t /*bytes_transferred*/) {
      if (ec == beast::websocket::error::closed ||
          ec == asio::error::operation_aborted) {
        return;
      }
      if (!ec) {
        received_count_++;
        std::string message = beast::buffers_to_string(buffer->data());
        OBCX_DEBUG("Received message #{}: {}", received_count_.load(), message);

        // Simulate weak network: delay the response.
        auto timer = std::make_shared<asio::steady_timer>(ioc_);
        timer->expires_after(std::chrono::milliseconds(WEAK_NETWORK_DELAY_MS));
        timer->async_wait([this, ws, timer](beast::error_code ec) {
          if (!ec) {
            queue_write("OK");
          }
        });

        start_read_loop(ws);
      }
    });
  }

  void queue_write(std::string message) {
    bool write_in_progress = !write_queue_.empty();
    write_queue_.push(std::move(message));

    if (!write_in_progress) {
      do_write();
    }
  }

  void do_write() {
    if (write_queue_.empty() || !ws_ || !ws_->is_open()) {
      return;
    }

    ws_->async_write(asio::buffer(write_queue_.front()),
                     [this](beast::error_code ec, std::size_t /*bytes*/) {
                       if (!ec) {
                         write_queue_.pop();
                         if (!write_queue_.empty()) {
                           do_write();
                         }
                       }
                     });
  }

  asio::io_context ioc_;
  tcp::endpoint endpoint_;
  tcp::acceptor acceptor_;
  asio::executor_work_guard<asio::io_context::executor_type> work_guard_;
  std::thread thread_;

  std::shared_ptr<beast::websocket::stream<tcp::socket>> ws_;
  std::atomic<bool> accepting_;
  std::atomic<size_t> received_count_{0};

  std::queue<std::string> write_queue_;
};

/**
 * WebSocket queue test fixture.
 */
class WebSocketQueueTest : public testing::Test {
protected:
  void SetUp() override {
    common::Logger::initialize(spdlog::level::trace);

    server_ = std::make_unique<MockWebSocketServer>("127.0.0.1");
    server_->start();

    std::this_thread::sleep_for(
        std::chrono::milliseconds(SERVER_STARTUP_DELAY));

    client_ = std::make_shared<network::WebsocketClient>(client_ioc_);
  }

  void TearDown() override {
    if (client_) {
      try {
        asio::co_spawn(
            client_ioc_,
            [this]() -> asio::awaitable<void> { co_await client_->close(); },
            asio::detached);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
      } catch (...) {
      }
    }

    work_guard_.reset();
    client_ioc_.stop();
    if (client_thread_.joinable()) {
      client_thread_.join();
    }

    if (server_) {
      server_->join_and_stop();
    }
  }

  void start_client_ioc() {
    client_thread_ = std::thread([this]() {
      work_guard_.emplace(client_ioc_.get_executor());
      client_ioc_.run();
    });
  }

  void connect_to_server() {
    asio::co_spawn(
        client_ioc_,
        [this]() -> asio::awaitable<void> {
          co_await client_->run(
              "127.0.0.1", std::to_string(server_->get_port()), "",
              [](const beast::error_code &ec, const std::string &msg) {
                if (ec) {
                  if (ec != asio::error::operation_aborted) {
                    OBCX_ERROR("WebSocket error: {}", ec.message());
                  }
                } else if (!msg.empty()) {
                  OBCX_DEBUG("Received message: {}", msg);
                }
              });
        },
        asio::detached);

    std::this_thread::sleep_for(
        std::chrono::milliseconds(CONNECTION_ESTABLISH_DELAY));
  }

  asio::io_context client_ioc_;
  std::unique_ptr<MockWebSocketServer> server_;
  std::shared_ptr<network::WebsocketClient> client_;
  std::thread client_thread_;
  std::optional<asio::executor_work_guard<asio::io_context::executor_type>>
      work_guard_;
};

/**
 * Test concurrent writes.
 */
TEST_F(WebSocketQueueTest, ConcurrentWrites) {
  start_client_ioc();
  connect_to_server();

  OBCX_INFO("Starting concurrent-writes test...");

  std::atomic<size_t> success_count{0};
  std::atomic<size_t> error_count{0};
  std::vector<std::future<void>> futures;

  for (size_t i = 0; i < CONCURRENT_WRITE_COUNT; ++i) {
    std::promise<void> promise;
    futures.push_back(promise.get_future());

    asio::co_spawn(
        client_ioc_,
        [this, i, &success_count, &error_count,
         p = std::move(promise)]() mutable -> asio::awaitable<void> {
          std::string message = "message " + std::to_string(i);
          OBCX_DEBUG("Sending message: {}", message);

          try {
            co_await client_->send(message);
            success_count++;
            OBCX_DEBUG("Message {} sent successfully", i);
          } catch (const std::exception &e) {
            error_count++;
            OBCX_ERROR("Message {} send failed: {}", i, e.what());
          }
          p.set_value();
        },
        asio::detached);
  }

  // Wait for all tasks to complete.
  for (auto &future : futures) {
    auto status = future.wait_for(std::chrono::seconds(5));
    ASSERT_EQ(status, std::future_status::ready)
        << "concurrent write tasks should finish within 5 seconds";
  }

  OBCX_INFO("Concurrent writes done: success={}, failed={}",
            success_count.load(), error_count.load());

  EXPECT_EQ(success_count.load(), CONCURRENT_WRITE_COUNT)
      << "all messages should be sent successfully";
  EXPECT_EQ(error_count.load(), 0) << "no message should fail to send";

  // Wait for the server to receive all messages.
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  EXPECT_EQ(server_->get_received_count(), CONCURRENT_WRITE_COUNT)
      << "server should have received every message";
}

/**
 * Test sequential writes under weak-network conditions.
 */
TEST_F(WebSocketQueueTest, WeakNetworkWrites) {
  start_client_ioc();
  connect_to_server();

  OBCX_INFO("Starting weak-network writes test...");

  std::atomic<size_t> success_count{0};
  std::atomic<size_t> error_count{0};
  std::promise<void> completion_promise;
  auto completion_future = completion_promise.get_future();

  asio::co_spawn(
      client_ioc_,
      [this, &success_count, &error_count,
       p = std::move(completion_promise)]() mutable -> asio::awaitable<void> {
        for (size_t i = 0; i < WEAK_NETWORK_WRITE_COUNT; ++i) {
          std::string message = "weak-net message " + std::to_string(i);
          OBCX_DEBUG("Sending weak-net message: {}", message);

          try {
            co_await client_->send(message);
            success_count++;
            OBCX_DEBUG("Weak-net message {} sent successfully", i);

            // Brief sleep to mimic application logic between sends.
            co_await asio::steady_timer(co_await asio::this_coro::executor,
                                        std::chrono::milliseconds(10))
                .async_wait(asio::use_awaitable);
          } catch (const std::exception &e) {
            error_count++;
            OBCX_ERROR("Weak-net message {} send failed: {}", i, e.what());
          }
        }
        p.set_value();
      },
      asio::detached);

  // Wait for the test to complete.
  auto status = completion_future.wait_for(std::chrono::seconds(10));
  ASSERT_EQ(status, std::future_status::ready)
      << "weak-network test should finish within 10 seconds";

  OBCX_INFO("Weak-network writes done: success={}, failed={}",
            success_count.load(), error_count.load());

  EXPECT_EQ(success_count.load(), WEAK_NETWORK_WRITE_COUNT)
      << "all messages should be sent successfully";
  EXPECT_EQ(error_count.load(), 0) << "no message should fail to send";

  // Wait for the server to receive all messages.
  std::this_thread::sleep_for(std::chrono::seconds(1));
  EXPECT_EQ(server_->get_received_count(), WEAK_NETWORK_WRITE_COUNT)
      << "server should have received every message";
}

} // namespace obcx::test
// NOLINTEND
