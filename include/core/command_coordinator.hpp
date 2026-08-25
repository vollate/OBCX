#ifndef OBCX_INCLUDE_CORE_COMMAND_COORDINATOR_HPP_
#define OBCX_INCLUDE_CORE_COMMAND_COORDINATOR_HPP_

#include "common/config_loader.hpp"
#include "core/actor_manager.hpp"
#include "core/command_matcher.hpp"
#include "core/command_platform_adapter.hpp"
#include "core/orchestrator.hpp"

#include <atomic>
#include <boost/asio/awaitable.hpp>
#include <chrono>
#include <compare>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace obcx::core {

inline constexpr std::string_view command_processed_header =
    command::processed_header;
inline constexpr std::string_view command_name_header = command::name_header;
inline constexpr std::string_view command_actor_header = command::actor_header;
inline constexpr std::string_view command_transaction_header =
    command::transaction_header;
inline constexpr std::string_view command_generation_header =
    command::generation_header;
inline constexpr std::string_view command_reply_header = command::reply_header;
inline constexpr std::string_view command_outcome_header =
    command::outcome_header;

struct CommandRouteKey {
  std::string platform;
  std::string bot;
  std::string command;

  auto operator<=>(const CommandRouteKey &) const = default;
};

struct CommandBotKey {
  std::string platform;
  std::string bot;

  auto operator<=>(const CommandBotKey &) const = default;
};

struct ActiveCommandRoute {
  CommandRouteKey key;
  std::string actor;
  std::string request_type;
  std::string description;
  std::string partition_expression = "global";
  std::string db_instance;
  std::string db_namespace;
  common::CommandFallback fallback = common::CommandFallback::Continue;
  std::chrono::milliseconds timeout{
      common::CommandRuntimeConfig::default_timeout_ms};
};

struct ActiveCommandPattern {
  std::string command;
  std::string expression;
  std::shared_ptr<const re2::RE2> compiled;
};

struct ActiveCommandBot {
  CommandBotKey key;
  std::string target;
  std::shared_ptr<ICommandPlatformAdapter> adapter;
  std::vector<CommandCatalogEntry> catalog;
  std::vector<ActiveCommandPattern> patterns;
};

class CommandRoutingTable {
public:
  [[nodiscard]] auto empty() const noexcept -> bool;
  [[nodiscard]] auto find_route(const CommandRouteKey &key) const noexcept
      -> const ActiveCommandRoute *;
  [[nodiscard]] auto find_bot(const CommandBotKey &key) const noexcept
      -> const ActiveCommandBot *;
  [[nodiscard]] auto routes() const noexcept
      -> const std::map<CommandRouteKey, ActiveCommandRoute> &;
  [[nodiscard]] auto bots() const noexcept
      -> const std::map<CommandBotKey, ActiveCommandBot> &;

private:
  friend struct CommandRoutingBuildResult;
  friend auto build_command_routing_table(
      const common::RuntimeConfigSnapshot &,
      const std::unordered_map<std::string, ActorInputContract> &)
      -> struct CommandRoutingBuildResult;

  std::map<CommandRouteKey, ActiveCommandRoute> routes_;
  std::map<CommandBotKey, ActiveCommandBot> bots_;
};

struct CommandRoutingBuildFailure {
  std::string code;
  std::string message;
};

struct CommandRoutingBuildResult {
  std::shared_ptr<const CommandRoutingTable> table;
  std::optional<CommandRoutingBuildFailure> failure;

  explicit operator bool() const noexcept {
    return table != nullptr && !failure.has_value();
  }
};

[[nodiscard]] auto normalize_command_platform(std::string platform)
    -> std::string;

[[nodiscard]] auto build_command_routing_table(
    const common::RuntimeConfigSnapshot &snapshot,
    const std::unordered_map<std::string, ActorInputContract> &contracts)
    -> CommandRoutingBuildResult;

class CommandCoordinator {
public:
  CommandCoordinator(std::uint64_t generation_id,
                     std::shared_ptr<const CommandRoutingTable> routing_table,
                     std::shared_ptr<NativeActorScheduler> scheduler,
                     std::shared_ptr<Orchestrator> orchestrator);

  auto process(MessageEnvelope message, std::shared_ptr<void> route_lifetime)
      -> boost::asio::awaitable<OrchestratorResult>;

  void shutdown() noexcept;

private:
  std::uint64_t generation_id_ = 0;
  std::shared_ptr<const CommandRoutingTable> routing_table_;
  std::shared_ptr<NativeActorScheduler> scheduler_;
  std::shared_ptr<Orchestrator> orchestrator_;
  std::atomic_uint64_t next_transaction_ = 1;
  std::atomic_bool shutdown_ = false;
};

} // namespace obcx::core

#endif // OBCX_INCLUDE_CORE_COMMAND_COORDINATOR_HPP_
