#include "network/detail/websocket_write_queue.hpp"

#include <boost/asio/error.hpp>
#include <boost/asio/experimental/channel_error.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/system_error.hpp>
#include <stdexcept>
#include <utility>

namespace obcx::network::detail {

struct WebsocketWriteQueue::WriteRequest {
  using CompletionChannel = asio::experimental::concurrent_channel<void(
      boost::system::error_code, std::exception_ptr)>;

  WriteRequest(asio::any_io_executor executor, std::string value)
      : message(std::move(value)), completion(std::move(executor), 1) {}

  void finish(std::exception_ptr failure) {
    bool expected = false;
    if (!finished.compare_exchange_strong(expected, true,
                                          std::memory_order_acq_rel)) {
      return;
    }
    static_cast<void>(
        completion.try_send(boost::system::error_code{}, std::move(failure)));
  }

  std::string message;
  CompletionChannel completion;
  std::atomic_bool finished{false};
};

WebsocketWriteQueue::WebsocketWriteQueue(asio::any_io_executor executor,
                                         const std::size_t capacity,
                                         WriteOperation write_operation,
                                         CancelOperation cancel_operation)
    : executor_(std::move(executor)), requests_(executor_, capacity),
      write_operation_(std::move(write_operation)),
      cancel_operation_(std::move(cancel_operation)) {
  if (capacity == 0) {
    throw std::invalid_argument(
        "WebSocket write queue capacity must be positive");
  }
  if (!write_operation_) {
    throw std::invalid_argument("WebSocket write operation is required");
  }
}

WebsocketWriteQueue::~WebsocketWriteQueue() { stop(); }

auto WebsocketWriteQueue::cancellation_failure() -> std::exception_ptr {
  return std::make_exception_ptr(
      boost::system::system_error{asio::error::operation_aborted});
}

auto WebsocketWriteQueue::terminal_failure() const -> std::exception_ptr {
  std::scoped_lock lock(state_mutex_);
  return terminal_failure_ ? terminal_failure_ : cancellation_failure();
}

void WebsocketWriteQueue::set_active(std::shared_ptr<WriteRequest> request) {
  std::scoped_lock lock(state_mutex_);
  active_ = std::move(request);
}

void WebsocketWriteQueue::clear_active(
    const std::shared_ptr<WriteRequest> &request) {
  std::scoped_lock lock(state_mutex_);
  if (active_ == request) {
    active_.reset();
  }
}

void WebsocketWriteQueue::drain_queued(const std::exception_ptr &failure) {
  while (
      requests_.try_receive([&failure](const boost::system::error_code &error,
                                       std::shared_ptr<WriteRequest> request) {
        if (!error && request) {
          request->finish(failure);
        }
      })) {
  }
}

auto WebsocketWriteQueue::send(std::string message) -> asio::awaitable<void> {
  if (stopped()) {
    std::rethrow_exception(terminal_failure());
  }

  auto request = std::make_shared<WriteRequest>(executor_, std::move(message));
  try {
    co_await requests_.async_send(boost::system::error_code{}, request,
                                  asio::use_awaitable);
  } catch (const boost::system::system_error &) {
    std::rethrow_exception(terminal_failure());
  }

  auto failure =
      co_await request->completion.async_receive(asio::use_awaitable);
  if (failure) {
    std::rethrow_exception(failure);
  }
}

auto WebsocketWriteQueue::run() -> asio::awaitable<void> {
  for (;;) {
    std::shared_ptr<WriteRequest> request;
    try {
      request = co_await requests_.async_receive(asio::use_awaitable);
    } catch (const boost::system::system_error &) {
      co_return;
    }
    if (!request) {
      continue;
    }

    set_active(request);
    if (stopped()) {
      request->finish(terminal_failure());
      clear_active(request);
      co_return;
    }

    try {
      co_await write_operation_(request->message);
      request->finish({});
      clear_active(request);
    } catch (...) {
      const auto failure = std::current_exception();
      request->finish(failure);
      clear_active(request);
      stop(failure);
      co_return;
    }
  }
}

void WebsocketWriteQueue::stop(std::exception_ptr failure) {
  if (!failure) {
    failure = cancellation_failure();
  }

  bool expected = false;
  if (!stopped_.compare_exchange_strong(expected, true,
                                        std::memory_order_acq_rel)) {
    return;
  }

  std::shared_ptr<WriteRequest> active;
  {
    std::scoped_lock lock(state_mutex_);
    terminal_failure_ = failure;
    active = active_;
  }

  requests_.close();
  if (cancel_operation_) {
    try {
      cancel_operation_();
    } catch (...) {
    }
  }
  if (active) {
    active->finish(failure);
  }
  drain_queued(failure);
}

} // namespace obcx::network::detail
