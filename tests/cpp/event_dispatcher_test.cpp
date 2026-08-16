#include "core/qq_bot.hpp"
#include "onebot11/adapter/protocol_adapter.hpp"

#include <boost/asio/post.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <thread>

namespace obcx::core {
namespace {

namespace asio = boost::asio;
using namespace std::chrono_literals;

class EventLoopProbeBot final : public QQBot {
public:
  EventLoopProbeBot() : QQBot(adapter::onebot11::ProtocolAdapter{}) {}

  void dispatch_message(common::MessageEvent event) {
    dispatcher_->dispatch(this, common::Event{std::move(event)});
  }

  void post_io(std::function<void()> handler) {
    asio::post(*io_context_, std::move(handler));
  }

  [[nodiscard]] auto io_executor() const -> asio::any_io_executor {
    return io_context_->get_executor();
  }
};

TEST(EventDispatcherTest, BotIoProgressesWhileEventHandlerAwaitsActorIngress) {
  EventLoopProbeBot bot;
  auto ingress_gate = std::make_shared<asio::steady_timer>(bot.io_executor());
  ingress_gate->expires_at(std::chrono::steady_clock::time_point::max());

  std::promise<std::thread::id> handler_entered_promise;
  auto handler_entered = handler_entered_promise.get_future();
  std::promise<std::thread::id> handler_completed_promise;
  auto handler_completed = handler_completed_promise.get_future();

  bot.on_event<common::MessageEvent>(
      [&handler_entered_promise, &handler_completed_promise,
       ingress_gate](IBot &, common::MessageEvent) -> asio::awaitable<void> {
        handler_entered_promise.set_value(std::this_thread::get_id());
        boost::system::error_code error;
        co_await ingress_gate->async_wait(
            asio::redirect_error(asio::use_awaitable, error));
        handler_completed_promise.set_value(std::this_thread::get_id());
      });

  bot.dispatch_message(common::MessageEvent{});
  std::thread io_thread([&bot] { bot.run(); });

  if (handler_entered.wait_for(2s) != std::future_status::ready) {
    bot.stop();
    io_thread.join();
    FAIL() << "event handler did not start on the bot I/O executor";
    return;
  }
  const auto handler_thread = handler_entered.get();

  std::promise<std::thread::id> marker_promise;
  auto marker = marker_promise.get_future();
  bot.post_io([&marker_promise] {
    marker_promise.set_value(std::this_thread::get_id());
  });
  const auto marker_status = marker.wait_for(200ms);
  EXPECT_EQ(marker_status, std::future_status::ready);
  EXPECT_EQ(handler_completed.wait_for(20ms), std::future_status::timeout);

  bot.post_io([ingress_gate] { ingress_gate->cancel(); });
  EXPECT_EQ(handler_completed.wait_for(2s), std::future_status::ready);
  if (marker_status == std::future_status::ready) {
    EXPECT_EQ(marker.get(), handler_thread);
  }
  if (handler_completed.valid() &&
      handler_completed.wait_for(0ms) == std::future_status::ready) {
    EXPECT_EQ(handler_completed.get(), handler_thread);
  }

  bot.stop();
  io_thread.join();
}

} // namespace
} // namespace obcx::core
