#ifndef OBCX_INCLUDE_COMMON_CONFIG_LOADER_HPP_
#define OBCX_INCLUDE_COMMON_CONFIG_LOADER_HPP_

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <toml++/toml.hpp>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace obcx::common {

enum class BotInstallationSurface : std::uint8_t {
  OneBot11Qq,
  TelegramBotApi,
};

enum class BotTransport : std::uint8_t {
  WebSocket,
  Http,
};

enum class BotProxyType : std::uint8_t {
  Http,
  Https,
  Socks5,
};

struct BotProxyConfig {
  std::string host;
  std::uint16_t port{};
  BotProxyType type = BotProxyType::Http;
  std::string username;
  std::string password;

  auto operator==(const BotProxyConfig &) const -> bool = default;
};

struct OneBot11WebSocketConnectionConfig {
  std::string host = "localhost";
  std::uint16_t port = 3001;
  std::string access_token;
  std::chrono::milliseconds connect_timeout{5'000};
  std::chrono::milliseconds action_timeout{30'000};

  auto operator==(const OneBot11WebSocketConnectionConfig &) const
      -> bool = default;
};

struct OneBot11HttpConnectionConfig {
  std::string host = "localhost";
  std::uint16_t port = 3000;
  std::string access_token;
  bool use_tls{};
  std::chrono::milliseconds connect_timeout{5'000};
  std::chrono::milliseconds action_timeout{30'000};
  std::chrono::milliseconds poll_interval{1'000};

  auto operator==(const OneBot11HttpConnectionConfig &) const -> bool = default;
};

struct TelegramHttpConnectionConfig {
  std::string host = "api.telegram.org";
  std::uint16_t port = 443;
  std::string access_token;
  std::string bot_username;
  bool use_tls = true;
  std::chrono::milliseconds connect_timeout{5'000};
  std::chrono::milliseconds action_timeout{30'000};
  std::chrono::milliseconds poll_timeout{25'000};
  std::chrono::milliseconds poll_force_close{30'000};
  std::chrono::milliseconds poll_retry_interval{3'000};
  std::optional<BotProxyConfig> proxy;

  auto operator==(const TelegramHttpConnectionConfig &) const -> bool = default;
};

using BotConnectionConfig =
    std::variant<OneBot11WebSocketConnectionConfig,
                 OneBot11HttpConnectionConfig, TelegramHttpConnectionConfig>;

struct BotInstallationConfig {
  std::string installation_id;
  bool enabled{};
  BotInstallationSurface surface{};
  BotTransport transport{};
  BotConnectionConfig connection;

  auto operator==(const BotInstallationConfig &) const -> bool = default;
};

[[nodiscard]] auto bot_surface_id(BotInstallationSurface surface) noexcept
    -> std::string_view;
[[nodiscard]] auto bot_transport_id(BotTransport transport) noexcept
    -> std::string_view;

struct ActorConfig {
  std::string name;
  std::string library;
  bool enabled{};
  std::vector<std::string> required; // enabled actors that must activate first
  std::string partition = "global";
  std::string db;
  std::string db_namespace;
};

struct DbInstanceConfig {
  std::string name;
  std::string type;
  std::string path;
  std::string dsn;
  toml::table config;
};

struct PipelineStageConfig {
  std::string name;
  std::string actor;
  std::string input;
  std::vector<std::string> outputs;
  std::vector<std::string> after;
  std::string mode;
};

struct PipelineConfig {
  std::string name;
  std::string source;
  std::vector<PipelineStageConfig> stages;
};

enum class CommandFallback {
  Continue,
  Consume,
};

struct CommandRouteConfig {
  std::string actor;
  std::vector<std::string> commands;
  std::vector<std::string> platforms;
  std::vector<std::string> bots;
  CommandFallback fallback = CommandFallback::Continue;
  size_t timeout_ms = 0; // 0 = inherit command_runtime.timeout_ms
};

struct CommandRuntimeConfig {
  static constexpr size_t default_timeout_ms = 5'000;
  static constexpr size_t min_timeout_ms = 100;
  static constexpr size_t max_timeout_ms = 300'000;

  size_t timeout_ms = default_timeout_ms;
  std::vector<CommandRouteConfig> routes;
};

enum class ActorSchedulerPolicy {
  Stealing,
  Sharing,
};

struct ActorRuntimeConfig {
  static constexpr size_t default_reload_drain_timeout_ms = 5'000;
  static constexpr size_t min_reload_drain_timeout_ms = 100;
  static constexpr size_t max_reload_drain_timeout_ms = 300'000;

  ActorSchedulerPolicy policy = ActorSchedulerPolicy::Stealing;
  size_t workers = 0;          // 0 = selected from the runtime thread budget
  size_t blocking_workers = 0; // 0 = selected from the runtime thread budget
  size_t slow_resume_warning_ms = 10;
  size_t routing_hop_limit = 32;
  size_t reload_drain_timeout_ms = default_reload_drain_timeout_ms;
};

struct ConfigValidationError {
  std::string code;
  std::string message;
  std::string pipeline;
  std::string stage;
  std::string actor;
  std::string dependency;
  std::string input;
};

struct RuntimeThreadFingerprintInput {
  size_t actor_workers = 0;
  size_t io_workers = 0;
  size_t blocking_workers = 0;

  auto operator==(const RuntimeThreadFingerprintInput &) const
      -> bool = default;
};

struct ProcessOwnedConfigFingerprint {
  std::string bots;
  std::string database_instances;
  RuntimeThreadFingerprintInput thread_budget;

  auto operator==(const ProcessOwnedConfigFingerprint &) const
      -> bool = default;
};

struct ConfigLoadDiagnostic {
  std::string code;
  std::string path;
  std::string message;
  size_t line = 0;
  size_t column = 0;
};

class RuntimeConfigSnapshot {
public:
  ~RuntimeConfigSnapshot() = default;
  RuntimeConfigSnapshot(const RuntimeConfigSnapshot &) = delete;
  auto operator=(const RuntimeConfigSnapshot &)
      -> RuntimeConfigSnapshot & = delete;
  RuntimeConfigSnapshot(RuntimeConfigSnapshot &&) = delete;
  auto operator=(RuntimeConfigSnapshot &&) -> RuntimeConfigSnapshot & = delete;

  [[nodiscard]] auto get_bot_configs() const
      -> std::vector<BotInstallationConfig>;
  [[nodiscard]] auto get_actor_configs() const -> std::vector<ActorConfig>;
  [[nodiscard]] auto get_db_instance_configs() const
      -> std::vector<DbInstanceConfig>;
  [[nodiscard]] auto get_pipeline_configs() const
      -> std::vector<PipelineConfig>;
  [[nodiscard]] auto get_command_runtime_config() const -> CommandRuntimeConfig;
  [[nodiscard]] auto get_actor_runtime_config() const -> ActorRuntimeConfig;
  [[nodiscard]] auto validate_actor_runtime_config() const
      -> std::vector<ConfigValidationError>;
  [[nodiscard]] auto validate_actor_pipeline_configs() const
      -> std::vector<ConfigValidationError>;
  [[nodiscard]] auto validate_actor_pipeline_contracts(
      const std::unordered_map<std::string, std::unordered_set<std::string>>
          &actor_inputs) const -> std::vector<ConfigValidationError>;

  template <typename T>
  [[nodiscard]] auto get_value(std::string_view key) const -> std::optional<T> {
    const auto node = config_data_.at_path(key);
    if (!node) {
      return std::nullopt;
    }
    return node.template value<T>();
  }

  template <typename T>
  [[nodiscard]] auto get_actor_value(std::string_view actor,
                                     std::string_view key) const
      -> std::optional<T> {
    const auto *actors = config_data_.get_as<toml::table>("actors");
    const auto *actor_table =
        actors == nullptr ? nullptr : actors->get_as<toml::table>(actor);
    const auto *actor_config = actor_table == nullptr
                                   ? nullptr
                                   : actor_table->get_as<toml::table>("config");
    if (actor_config == nullptr) {
      return std::nullopt;
    }
    const auto node = actor_config->at_path(key);
    if (!node) {
      return std::nullopt;
    }
    return node.template value<T>();
  }

  [[nodiscard]] auto get_section(std::string_view section_name) const
      -> std::optional<toml::table>;
  [[nodiscard]] auto get_actor_section(std::string_view actor,
                                       std::string_view section_name = {}) const
      -> std::optional<toml::table>;
  [[nodiscard]] auto process_owned_fingerprint(
      RuntimeThreadFingerprintInput thread_budget) const
      -> ProcessOwnedConfigFingerprint;
  [[nodiscard]] auto config_path() const noexcept -> const std::string & {
    return config_path_;
  }

private:
  friend class ConfigLoader;

  RuntimeConfigSnapshot(std::string config_path, toml::table config_data)
      : config_path_(std::move(config_path)),
        config_data_(std::move(config_data)) {}

  std::string config_path_;
  toml::table config_data_;
};

struct RuntimeConfigBuildResult {
  std::shared_ptr<const RuntimeConfigSnapshot> snapshot;
  std::optional<ConfigLoadDiagnostic> diagnostic;

  explicit operator bool() const noexcept { return snapshot != nullptr; }
};

class ActorConfigView {
public:
  ActorConfigView() = default;
  ActorConfigView(std::shared_ptr<const RuntimeConfigSnapshot> snapshot,
                  std::string actor)
      : snapshot_(std::move(snapshot)), actor_(std::move(actor)) {}

  [[nodiscard]] auto available() const noexcept -> bool {
    return snapshot_ != nullptr;
  }
  [[nodiscard]] auto actor() const noexcept -> const std::string & {
    return actor_;
  }

  template <typename T>
  [[nodiscard]] auto get_value(std::string_view key) const -> std::optional<T> {
    return snapshot_ == nullptr
               ? std::nullopt
               : snapshot_->template get_actor_value<T>(actor_, key);
  }

  [[nodiscard]] auto get_section(std::string_view section_name = {}) const
      -> std::optional<toml::table> {
    return snapshot_ == nullptr
               ? std::nullopt
               : snapshot_->get_actor_section(actor_, section_name);
  }

  [[nodiscard]] auto get_root_section(std::string_view section_name) const
      -> std::optional<toml::table> {
    return snapshot_ == nullptr ? std::nullopt
                                : snapshot_->get_section(section_name);
  }

private:
  std::shared_ptr<const RuntimeConfigSnapshot> snapshot_;
  std::string actor_;
};

class ActorConfigService {
public:
  explicit ActorConfigService(
      std::shared_ptr<const RuntimeConfigSnapshot> snapshot)
      : snapshot_(std::move(snapshot)) {}

  [[nodiscard]] auto for_actor(std::string actor) const -> ActorConfigView {
    return ActorConfigView{snapshot_, std::move(actor)};
  }

private:
  std::shared_ptr<const RuntimeConfigSnapshot> snapshot_;
};

[[nodiscard]] auto changed_process_owned_domains(
    const ProcessOwnedConfigFingerprint &active,
    const ProcessOwnedConfigFingerprint &candidate) -> std::vector<std::string>;
[[nodiscard]] auto describe_process_owned_changes(
    const ProcessOwnedConfigFingerprint &active,
    const ProcessOwnedConfigFingerprint &candidate) -> std::string;

class ConfigLoader {
  ConfigLoader() = default;
  ~ConfigLoader() = default;

public:
  static auto instance() -> ConfigLoader &;

  ConfigLoader(const ConfigLoader &) = delete;
  auto operator=(const ConfigLoader &) -> ConfigLoader & = delete;
  ConfigLoader(ConfigLoader &&) = delete;
  auto operator=(ConfigLoader &&) -> ConfigLoader & = delete;

  [[nodiscard]] static auto build_snapshot(const std::string &config_path)
      -> RuntimeConfigBuildResult;
  void publish_snapshot(
      std::shared_ptr<const RuntimeConfigSnapshot> snapshot) noexcept;
  [[nodiscard]] auto current_snapshot() const noexcept
      -> std::shared_ptr<const RuntimeConfigSnapshot>;

  auto load_config(const std::string &config_path) -> bool;

  auto get_bot_configs() const -> std::vector<BotInstallationConfig>;

  auto get_actor_configs() const -> std::vector<ActorConfig>;

  auto get_db_instance_configs() const -> std::vector<DbInstanceConfig>;

  auto get_pipeline_configs() const -> std::vector<PipelineConfig>;

  auto get_command_runtime_config() const -> CommandRuntimeConfig;

  auto get_actor_runtime_config() const -> ActorRuntimeConfig;

  auto validate_actor_runtime_config() const
      -> std::vector<ConfigValidationError>;

  auto validate_actor_pipeline_configs() const
      -> std::vector<ConfigValidationError>;

  auto validate_actor_pipeline_contracts(
      const std::unordered_map<std::string, std::unordered_set<std::string>>
          &actor_inputs) const -> std::vector<ConfigValidationError>;

  template <typename T>
  auto get_value(const std::string &key) const -> std::optional<T> {
    const auto snapshot = current_snapshot();
    return snapshot == nullptr ? std::nullopt
                               : snapshot->template get_value<T>(key);
  }

  auto get_section(const std::string &section_name) const
      -> std::optional<toml::table>;

  auto reload_config() -> bool;

  [[nodiscard]] auto is_loaded() const noexcept -> bool {
    return current_snapshot() != nullptr;
  }

  [[nodiscard]] auto get_config_path() const -> std::string;

private:
  std::atomic<std::shared_ptr<const RuntimeConfigSnapshot>> active_snapshot_;
};

} // namespace obcx::common

#endif // OBCX_INCLUDE_COMMON_CONFIG_LOADER_HPP_
