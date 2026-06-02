#pragma once

#include "common/logger.hpp"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <chrono>
#include <exception>
#include <future>
#include <memory>
#include <sstream>
#include <thread>

namespace obcx::core {
namespace asio = boost::asio;

/**
 * @brief 基于 Boost.Asio async_compose 的优雅任务调度器
 *
 * 该调度器能够将 CPU 密集型任务调度到专用线程池中执行，
 * 同时保持协程的线性、无回调编程模型。使用 async_compose
 * 确保与 Asio 异步模型的完美集成。
 */
class TaskScheduler {
public:
  /**
   * @brief 构造函数
   * @param thread_count 线程池中的线程数量，默认为硬件并发数
   */
  explicit TaskScheduler(
      std::size_t thread_count = std::thread::hardware_concurrency())
      : thread_pool_(thread_count) {
    OBCX_INFO("TaskScheduler created with thread pool size: {}", thread_count);
  }

  /**
   * @brief 获取线程池的IO上下文
   * @return asio::thread_pool& 线程池引用
   */
  auto get_io_context() -> asio::thread_pool & { return thread_pool_; }

  /**
   * @brief 析构函数，确保线程池正确关闭
   */
  ~TaskScheduler() {
    stop();
    OBCX_INFO("TaskScheduler destroyed");
  }

  /**
   * @brief 停止任务调度器
   */
  void stop() {
    if (!stopped_) {
      OBCX_INFO("Stopping TaskScheduler...");
      thread_pool_.stop();
      thread_pool_.join();
      stopped_ = true;
      OBCX_INFO("TaskScheduler stopped");
    }
  }

  /**
   * @brief 【核心功能】将重负载任务调度到线程池中执行
   *
   * 这是基于 async_compose 的优雅实现，完全消除了回调感，
   * 将复杂的异步逻辑封装成单一的 awaitable 对象。
   *
   * @tparam Func 可调用对象类型
   * @param task 要在线程池中执行的任务函数
   * @return asio::awaitable<T> 可等待的协程对象，T 是 task 的返回类型
   *
   * @example
   * ```cpp
   * // 使用示例
   * std::string result = co_await scheduler.run_heavy_task([data]() {
   *     return process_heavy_computation(data);
   * });
   * ```
   */
  template <typename Func>
  auto run_heavy_task(Func task)
      -> asio::awaitable<std::invoke_result_t<Func>, asio::any_io_executor> {

    using ReturnType = std::invoke_result_t<Func>;

    std::stringstream ss;
    ss << std::this_thread::get_id();
    OBCX_DEBUG(
        "TaskScheduler: Submitting heavy task to thread pool (thread ID: {})",
        ss.str());

    // promise/future bridges the thread pool task back to the coroutine.
    auto promise = std::make_shared<std::promise<ReturnType>>();
    auto future = promise->get_future();

    asio::post(
        thread_pool_, [task = std::move(task), promise]() mutable -> auto {
          try {
            std::stringstream worker_ss;
            worker_ss << std::this_thread::get_id();
            OBCX_DEBUG("TaskScheduler: Starting heavy task execution (worker "
                       "thread ID: {})",
                       worker_ss.str());

            if constexpr (std::is_void_v<ReturnType>) {
              task();
              OBCX_DEBUG(
                  "TaskScheduler: Heavy task completed (no return value)");
              promise->set_value();
            } else {
              auto result = task();
              OBCX_DEBUG(
                  "TaskScheduler: Heavy task completed, returning result");
              promise->set_value(std::move(result));
            }
          } catch (...) {
            OBCX_ERROR("TaskScheduler: Exception occurred during heavy task "
                       "execution");
            promise->set_exception(std::current_exception());
          }
        });

    // Poll the future on a 1ms timer so we yield to other coroutines on the
    // io executor instead of blocking it with future.get().
    while (future.wait_for(std::chrono::milliseconds(1)) !=
           std::future_status::ready) {
      co_await asio::steady_timer(co_await asio::this_coro::executor,
                                  std::chrono::milliseconds(1))
          .async_wait(asio::use_awaitable);
    }

    if constexpr (std::is_void_v<ReturnType>) {
      future.get();
      co_return;
    } else {
      co_return future.get();
    }
  }

  /**
   * @brief 批量执行多个重负载任务
   *
   * @tparam Func 可调用对象类型
   * @param tasks 任务函数列表
   * @return asio::awaitable<std::vector<T>> 所有任务结果的向量
   */
  template <typename Func>
  auto run_heavy_tasks_batch(std::vector<Func> tasks)
      -> asio::awaitable<std::vector<std::invoke_result_t<Func>>,
                         asio::any_io_executor> {

    using ReturnType = std::invoke_result_t<Func>;
    std::vector<ReturnType> results;
    results.reserve(tasks.size());

    OBCX_INFO("TaskScheduler: Starting batch execution of {} heavy tasks",
              tasks.size());

    for (auto &task : tasks) {
      auto result = co_await run_heavy_task(std::move(task));
      results.push_back(std::move(result));
    }

    OBCX_INFO("TaskScheduler: Batch task execution completed");
    co_return results;
  }

  /**
   * @brief 带超时的任务执行
   *
   * @tparam Func 可调用对象类型
   * @param task 任务函数
   * @param timeout 超时时间
   * @return asio::awaitable<std::optional<T>> 如果超时则返回
   * std::nullopt
   */
  template <typename Func>
  auto run_heavy_task_with_timeout(Func task, std::chrono::milliseconds timeout)
      -> asio::awaitable<std::optional<std::invoke_result_t<Func>>,
                         asio::any_io_executor> {

    using ReturnType = std::invoke_result_t<Func>;

    OBCX_DEBUG(
        "TaskScheduler: Executing heavy task with timeout (timeout: {}ms)",
        timeout.count());

    try {
      // TODO: implement real timeout via asio::steady_timer; today this just
      // forwards to run_heavy_task and does NOT enforce the timeout.
      auto result = co_await run_heavy_task(std::move(task));
      co_return std::make_optional(std::move(result));
    } catch (const std::exception &e) {
      OBCX_ERROR("TaskScheduler: Timeout task execution failed: {}", e.what());
      co_return std::nullopt;
    }
  }

private:
  asio::thread_pool thread_pool_;
  bool stopped_ = false;
};

} // namespace obcx::core
