#include "interfaces/bot.hpp"
#include "core/event_dispatcher.hpp"
#include "core/task_scheduler.hpp"

#include <boost/asio/io_context.hpp>

namespace obcx::core {

IBot::IBot(std::unique_ptr<adapter::BaseProtocolAdapter> adapter,
           std::shared_ptr<TaskScheduler> task_scheduler)
    : io_context_(std::make_shared<asio::io_context>()),
      adapter_{std::move(adapter)},
      task_scheduler_{task_scheduler ? std::move(task_scheduler)
                                     : std::make_shared<TaskScheduler>()},
      dispatcher_{
          std::make_unique<EventDispatcher>(task_scheduler_->get_io_context())},
      connection_manager_{nullptr} {}

IBot::~IBot() {
  // Stop the task scheduler first (if we're the sole owner). This must
  // happen before we tear down the connection manager so any in-flight
  // heavy task that touches the connection manager has already finished.
  if (task_scheduler_ && task_scheduler_.use_count() == 1) {
    task_scheduler_->stop();
  }
  task_scheduler_.reset();

  if (dispatcher_) {
    dispatcher_.reset();
  }

  // Destruction ordering matters: connection_manager_ holds members whose
  // destructors touch io_context services (notably asio::strand, which keeps
  // a shared_ptr to strand_executor_service::strand_impl backed by the
  // io_context's allocator; releasing that pointer after io_context_ is gone
  // crashes inside _Sp_counted_ptr_inplace::_M_destroy — observed SIGSEGV).
  //
  // Conversely, io_context_'s destructor tears down pending coroutine
  // frames that may capture 'this' into members of connection_manager_.
  //
  // Approach:
  //   1. Stop the io_context and drain any ready handlers (including the
  //      detached close() coroutine posted from the connection manager's
  //      own destructor below) so no coroutine frame outlives
  //      connection_manager_.
  //   2. Destroy connection_manager_ — its concrete destructor
  //      (e.g. ~WebSocketConnectionManager) performs its own cleanup while
  //      io_context_ is still alive.
  //   3. Destroy io_context_.
  if (io_context_) {
    io_context_->stop();
    try {
      io_context_->restart();
      io_context_->poll();
      io_context_->stop();
    } catch (const std::exception &) { // NOLINT(bugprone-empty-catch)
      // Destructor context: nothing actionable.
    } catch (...) { // NOLINT(bugprone-empty-catch)
      // Same as above for non-std exceptions.
    }
  }

  connection_manager_.reset();

  if (io_context_) {
    io_context_.reset();
  }
}

} // namespace obcx::core
