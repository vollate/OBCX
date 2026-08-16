#include "onebot11/network/websocket/detail/action_request_tracker.hpp"

#include <boost/asio/error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/system_error.hpp>
#include <stdexcept>
#include <utility>

namespace obcx::network::detail {
namespace {

class AsioActionDeadline final
    : public ActionDeadline,
      public std::enable_shared_from_this<AsioActionDeadline> {
public:
  AsioActionDeadline(asio::any_io_executor executor,
                     const std::chrono::milliseconds timeout)
      : timer_(std::move(executor), timeout) {}

  void start(std::function<void()> expired) {
    timer_.async_wait([self = shared_from_this(), expired = std::move(expired)](
                          const boost::system::error_code &error) {
      if (!error) {
        expired();
      }
    });
  }

  void cancel() override {
    try {
      static_cast<void>(timer_.cancel());
    } catch (...) {
    }
  }

private:
  asio::steady_timer timer_;
};

} // namespace

auto PendingAction::wait() -> asio::awaitable<ActionTerminalOutcome> {
  co_return co_await completion_.async_receive(asio::use_awaitable);
}

void PendingAction::finish(ActionTerminalOutcome outcome) {
  static_cast<void>(
      completion_.try_send(boost::system::error_code{}, std::move(outcome)));
}

auto default_action_deadline_factory() -> ActionDeadlineFactory {
  return [](asio::any_io_executor executor,
            const std::chrono::milliseconds timeout,
            std::function<void()> expired) -> std::shared_ptr<ActionDeadline> {
    auto deadline =
        std::make_shared<AsioActionDeadline>(std::move(executor), timeout);
    deadline->start(std::move(expired));
    return deadline;
  };
}

ActionRequestTracker::ActionRequestTracker(
    asio::any_io_executor executor, ActionDeadlineFactory deadline_factory)
    : executor_(std::move(executor)),
      deadline_factory_(deadline_factory ? std::move(deadline_factory)
                                         : default_action_deadline_factory()) {}

ActionRequestTracker::~ActionRequestTracker() { cancel_all(); }

auto ActionRequestTracker::start(const std::uint64_t echo,
                                 const std::chrono::milliseconds timeout)
    -> std::shared_ptr<PendingAction> {
  if (timeout <= std::chrono::milliseconds::zero()) {
    throw std::invalid_argument("OneBot action timeout must be positive");
  }

  auto request = std::shared_ptr<PendingAction>(new PendingAction{executor_});
  {
    std::scoped_lock lock(mutex_);
    if (pending_.contains(echo)) {
      throw std::invalid_argument("duplicate pending OneBot action echo");
    }
    pending_.emplace(echo, request);
  }

  const auto weak = weak_from_this();
  try {
    request->deadline_ = deadline_factory_(executor_, timeout, [weak, echo] {
      if (const auto tracker = weak.lock()) {
        static_cast<void>(tracker->finish(
            echo,
            ActionTerminalOutcome{.status = ActionTerminalStatus::Timeout}));
      }
    });
    if (!request->deadline_) {
      throw std::runtime_error("OneBot action deadline factory returned null");
    }
  } catch (...) {
    static_cast<void>(fail(echo, std::current_exception()));
    throw;
  }
  return request;
}

auto ActionRequestTracker::respond(const std::uint64_t echo,
                                   std::string response) -> bool {
  return finish(echo, ActionTerminalOutcome{
                          .status = ActionTerminalStatus::Response,
                          .response = std::move(response),
                      });
}

auto ActionRequestTracker::fail(const std::uint64_t echo,
                                std::exception_ptr failure) -> bool {
  if (!failure) {
    failure = std::make_exception_ptr(
        std::runtime_error{"OneBot action transport failed"});
  }
  return finish(echo, ActionTerminalOutcome{
                          .status = ActionTerminalStatus::TransportFailure,
                          .failure = std::move(failure),
                      });
}

auto ActionRequestTracker::cancel(const std::uint64_t echo) -> bool {
  return finish(
      echo, ActionTerminalOutcome{
                .status = ActionTerminalStatus::Cancelled,
                .failure = std::make_exception_ptr(boost::system::system_error{
                    asio::error::operation_aborted}),
            });
}

void ActionRequestTracker::fail_all(std::exception_ptr failure) {
  for (const auto echo : pending_echoes()) {
    static_cast<void>(fail(echo, failure));
  }
}

void ActionRequestTracker::cancel_all() {
  std::vector<std::shared_ptr<PendingAction>> requests;
  {
    std::scoped_lock lock(mutex_);
    requests.reserve(pending_.size());
    for (auto &[echo, request] : pending_) {
      requests.push_back(std::move(request));
    }
    pending_.clear();
  }

  for (const auto &request : requests) {
    if (request->deadline_) {
      request->deadline_->cancel();
    }
    request->finish(ActionTerminalOutcome{
        .status = ActionTerminalStatus::Cancelled,
        .failure = std::make_exception_ptr(
            boost::system::system_error{asio::error::operation_aborted}),
    });
  }
}

auto ActionRequestTracker::pending_count() const -> std::size_t {
  std::scoped_lock lock(mutex_);
  return pending_.size();
}

auto ActionRequestTracker::pending_echoes() const
    -> std::vector<std::uint64_t> {
  std::vector<std::uint64_t> echoes;
  std::scoped_lock lock(mutex_);
  echoes.reserve(pending_.size());
  for (const auto &[echo, request] : pending_) {
    echoes.push_back(echo);
  }
  return echoes;
}

auto ActionRequestTracker::finish(const std::uint64_t echo,
                                  ActionTerminalOutcome outcome) -> bool {
  std::shared_ptr<PendingAction> request;
  {
    std::scoped_lock lock(mutex_);
    const auto found = pending_.find(echo);
    if (found == pending_.end()) {
      return false;
    }
    request = std::move(found->second);
    pending_.erase(found);
  }

  if (request->deadline_) {
    request->deadline_->cancel();
  }
  request->finish(std::move(outcome));
  return true;
}

} // namespace obcx::network::detail
