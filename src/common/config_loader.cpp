#include "common/config_loader.hpp"
#include "common/logger.hpp"

#include <openssl/sha.h>

#include <algorithm>
#include <array>
#include <iomanip>
#include <queue>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace obcx::common {

auto bot_surface_id(const BotInstallationSurface surface) noexcept
    -> std::string_view {
  switch (surface) {
  case BotInstallationSurface::OneBot11Qq:
    return "onebot11.qq";
  case BotInstallationSurface::TelegramBotApi:
    return "telegram.bot_api";
  }
  return {};
}

auto bot_transport_id(const BotTransport transport) noexcept
    -> std::string_view {
  switch (transport) {
  case BotTransport::WebSocket:
    return "websocket";
  case BotTransport::Http:
    return "http";
  }
  return {};
}

auto ConfigLoader::instance() -> ConfigLoader & {
  static ConfigLoader instance;
  return instance;
}

namespace {

auto get_string_value(const toml::table &table, std::string_view key,
                      const std::string &default_value = "") -> std::string {
  if (const auto node = table.get(key)) {
    if (auto value = node->value<std::string>()) {
      return *value;
    }
  }

  return default_value;
}

auto get_bool_value(const toml::table &table, std::string_view key,
                    const bool default_value = false) -> bool {
  if (const auto node = table.get(key)) {
    if (auto value = node->value<bool>()) {
      return *value;
    }
  }

  return default_value;
}

auto get_non_negative_size(const toml::table &table, std::string_view key,
                           const size_t default_value) -> size_t {
  if (const auto node = table.get(key)) {
    if (const auto value = node->value<int64_t>(); value && *value >= 0) {
      return static_cast<size_t>(*value);
    }
  }

  return default_value;
}

class BotConfigurationError final : public std::runtime_error {
public:
  BotConfigurationError(std::string code, std::string path,
                        const std::string &message)
      : std::runtime_error(message), code_(std::move(code)),
        path_(std::move(path)) {}

  [[nodiscard]] auto code() const noexcept -> const std::string & {
    return code_;
  }
  [[nodiscard]] auto path() const noexcept -> const std::string & {
    return path_;
  }

private:
  std::string code_;
  std::string path_;
};

[[noreturn]] void bot_configuration_error(std::string code, std::string path,
                                          const std::string &message) {
  throw BotConfigurationError(std::move(code), std::move(path), message);
}

void validate_keys(const toml::table &table,
                   const std::unordered_set<std::string_view> &allowed,
                   const std::string_view path, const bool bot_table) {
  for (const auto &[key, value] : table) {
    (void)value;
    const auto key_view = key.str();
    if (allowed.contains(key_view)) {
      continue;
    }
    const auto field = std::string{path} + "." + std::string{key_view};
    const auto legacy =
        (bot_table && (key_view == "type" || key_view == "plugins")) ||
        (!bot_table &&
         (key_view == "type" || key_view == "timeout" ||
          key_view == "connect_timeout" || key_view == "action_timeout" ||
          key_view == "poll_timeout" || key_view == "poll_force_close" ||
          key_view == "poll_retry_interval" ||
          key_view == "heartbeat_interval" || key_view == "use_ssl" ||
          key_view == "secret"));
    bot_configuration_error(
        legacy ? "legacy_bot_configuration_key"
               : "unknown_bot_configuration_key",
        field,
        legacy ? field + " is a legacy key; use exact surface/transport and "
                         "explicit *_ms/use_tls fields"
               : field + " is not supported");
  }
}

auto required_string(const toml::table &table, const std::string_view key,
                     const std::string_view path) -> std::string {
  const auto *node = table.get(key);
  if (node == nullptr) {
    const auto field = std::string{path} + "." + std::string{key};
    bot_configuration_error("missing_bot_configuration_value", field,
                            field + " must be specified explicitly");
  }
  const auto value = node->value<std::string>();
  if (!value || value->empty()) {
    const auto field = std::string{path} + "." + std::string{key};
    bot_configuration_error("invalid_bot_configuration_value", field,
                            field + " must be a non-empty string");
  }
  return *value;
}

auto required_string_value(const toml::table &table, const std::string_view key,
                           const std::string_view path) -> std::string {
  const auto *node = table.get(key);
  if (node == nullptr) {
    const auto field = std::string{path} + "." + std::string{key};
    bot_configuration_error("missing_bot_configuration_value", field,
                            field + " must be specified explicitly");
  }
  const auto value = node->value<std::string>();
  if (!value) {
    const auto field = std::string{path} + "." + std::string{key};
    bot_configuration_error("invalid_bot_configuration_value", field,
                            field + " must be a string");
  }
  return *value;
}

auto required_bool(const toml::table &table, const std::string_view key,
                   const std::string_view path) -> bool {
  const auto *node = table.get(key);
  const auto value =
      node == nullptr ? std::optional<bool>{} : node->value<bool>();
  if (!value) {
    const auto field = std::string{path} + "." + std::string{key};
    bot_configuration_error("invalid_bot_configuration_value", field,
                            field + " must be a boolean");
  }
  return *value;
}

auto required_port(const toml::table &table, const std::string_view path)
    -> std::uint16_t {
  const auto *node = table.get("port");
  if (node == nullptr) {
    const auto field = std::string{path} + ".port";
    bot_configuration_error("missing_bot_configuration_value", field,
                            field + " must be specified explicitly");
  }
  const auto value = node->value<std::int64_t>();
  if (!value || *value <= 0 || *value > 65'535) {
    const auto field = std::string{path} + ".port";
    bot_configuration_error("invalid_bot_configuration_value", field,
                            field + " must be an integer from 1 to 65535");
  }
  return static_cast<std::uint16_t>(*value);
}

auto required_duration(const toml::table &table, const std::string_view key,
                       const std::string_view path)
    -> std::chrono::milliseconds {
  const auto *node = table.get(key);
  if (node == nullptr) {
    const auto field = std::string{path} + "." + std::string{key};
    bot_configuration_error("missing_bot_configuration_value", field,
                            field + " must be specified explicitly");
  }
  const auto value = node->value<std::int64_t>();
  constexpr std::int64_t maximum_duration_ms = 300'000;
  if (!value || *value <= 0 || *value > maximum_duration_ms) {
    const auto field = std::string{path} + "." + std::string{key};
    bot_configuration_error(
        "invalid_bot_configuration_value", field,
        field + " must be a positive millisecond value no greater than 300000");
  }
  return std::chrono::milliseconds{*value};
}

auto parse_surface(const std::string_view value, const std::string_view path)
    -> BotInstallationSurface {
  if (value == "onebot11.qq") {
    return BotInstallationSurface::OneBot11Qq;
  }
  if (value == "telegram.bot_api") {
    return BotInstallationSurface::TelegramBotApi;
  }
  bot_configuration_error(
      "unsupported_bot_surface", std::string{path},
      std::string{path} + " must be exactly onebot11.qq or telegram.bot_api");
}

auto parse_transport(const std::string_view value, const std::string_view path)
    -> BotTransport {
  if (value == "websocket") {
    return BotTransport::WebSocket;
  }
  if (value == "http") {
    return BotTransport::Http;
  }
  bot_configuration_error("unsupported_bot_transport", std::string{path},
                          std::string{path} +
                              " must be exactly websocket or http");
}

auto parse_proxy_type(const std::string_view value, const std::string_view path)
    -> BotProxyType {
  if (value == "http") {
    return BotProxyType::Http;
  }
  if (value == "https") {
    return BotProxyType::Https;
  }
  if (value == "socks5") {
    return BotProxyType::Socks5;
  }
  bot_configuration_error("unsupported_bot_proxy_type", std::string{path},
                          std::string{path} +
                              " must be http, https, or socks5");
}

auto parse_onebot_websocket_connection(const toml::table &table,
                                       const std::string_view path)
    -> OneBot11WebSocketConnectionConfig {
  static const std::unordered_set<std::string_view> keys = {
      "host", "port", "access_token", "connect_timeout_ms",
      "action_timeout_ms"};
  validate_keys(table, keys, path, false);
  OneBot11WebSocketConnectionConfig config;
  config.host = required_string(table, "host", path);
  config.port = required_port(table, path);
  config.access_token = required_string_value(table, "access_token", path);
  config.connect_timeout = required_duration(table, "connect_timeout_ms", path);
  config.action_timeout = required_duration(table, "action_timeout_ms", path);
  return config;
}

auto parse_onebot_http_connection(const toml::table &table,
                                  const std::string_view path)
    -> OneBot11HttpConnectionConfig {
  static const std::unordered_set<std::string_view> keys = {
      "host",
      "port",
      "access_token",
      "use_tls",
      "connect_timeout_ms",
      "action_timeout_ms",
      "poll_interval_ms"};
  validate_keys(table, keys, path, false);
  OneBot11HttpConnectionConfig config;
  config.host = required_string(table, "host", path);
  config.port = required_port(table, path);
  config.access_token = required_string_value(table, "access_token", path);
  config.use_tls = required_bool(table, "use_tls", path);
  config.connect_timeout = required_duration(table, "connect_timeout_ms", path);
  config.action_timeout = required_duration(table, "action_timeout_ms", path);
  config.poll_interval = required_duration(table, "poll_interval_ms", path);
  return config;
}

auto parse_telegram_http_connection(const toml::table &table,
                                    const std::string_view path)
    -> TelegramHttpConnectionConfig {
  static const std::unordered_set<std::string_view> keys = {
      "host",
      "port",
      "access_token",
      "bot_username",
      "use_tls",
      "connect_timeout_ms",
      "action_timeout_ms",
      "poll_timeout_ms",
      "poll_force_close_ms",
      "poll_retry_interval_ms",
      "proxy_host",
      "proxy_port",
      "proxy_type",
      "proxy_username",
      "proxy_password"};
  validate_keys(table, keys, path, false);
  TelegramHttpConnectionConfig config;
  config.host = required_string(table, "host", path);
  config.port = required_port(table, path);
  config.access_token = required_string(table, "access_token", path);
  config.bot_username = required_string_value(table, "bot_username", path);
  config.use_tls = required_bool(table, "use_tls", path);
  if (!config.use_tls) {
    bot_configuration_error(
        "invalid_bot_tls_configuration", std::string{path} + ".use_tls",
        std::string{path} + ".use_tls must be true for telegram.bot_api");
  }
  config.connect_timeout = required_duration(table, "connect_timeout_ms", path);
  config.action_timeout = required_duration(table, "action_timeout_ms", path);
  config.poll_timeout = required_duration(table, "poll_timeout_ms", path);
  config.poll_force_close =
      required_duration(table, "poll_force_close_ms", path);
  config.poll_retry_interval =
      required_duration(table, "poll_retry_interval_ms", path);
  if (config.poll_force_close < config.poll_timeout) {
    bot_configuration_error(
        "invalid_bot_configuration_value",
        std::string{path} + ".poll_force_close_ms",
        std::string{path} +
            ".poll_force_close_ms must be at least poll_timeout_ms");
  }

  const auto proxy_host = table.contains("proxy_host")
                              ? required_string(table, "proxy_host", path)
                              : std::string{};
  const auto proxy_port_node = table.get("proxy_port");
  if (proxy_host.empty() != (proxy_port_node == nullptr)) {
    bot_configuration_error(
        "invalid_bot_proxy_configuration", std::string{path} + ".proxy_host",
        std::string{path} + " requires proxy_host and proxy_port together");
  }
  if (!proxy_host.empty()) {
    BotProxyConfig proxy;
    proxy.host = proxy_host;
    if (const auto *node = table.get("proxy_port")) {
      const auto value = node->value<std::int64_t>();
      if (!value || *value <= 0 || *value > 65'535) {
        bot_configuration_error(
            "invalid_bot_proxy_configuration",
            std::string{path} + ".proxy_port",
            std::string{path} +
                ".proxy_port must be an integer from 1 to 65535");
      }
      proxy.port = static_cast<std::uint16_t>(*value);
    }
    proxy.type = parse_proxy_type(required_string(table, "proxy_type", path),
                                  std::string{path} + ".proxy_type");
    proxy.username = required_string_value(table, "proxy_username", path);
    proxy.password = required_string_value(table, "proxy_password", path);
    config.proxy = std::move(proxy);
  } else if (table.contains("proxy_type") || table.contains("proxy_username") ||
             table.contains("proxy_password")) {
    bot_configuration_error(
        "invalid_bot_proxy_configuration", std::string{path} + ".proxy_host",
        std::string{path} +
            " requires proxy_host and proxy_port before proxy options");
  }
  return config;
}

auto parse_bot_installations(const toml::table &document)
    -> std::vector<BotInstallationConfig> {
  std::vector<BotInstallationConfig> configurations;
  const auto *bots = document.get_as<toml::table>("bots");
  if (bots == nullptr) {
    return configurations;
  }
  configurations.reserve(bots->size());
  for (const auto &[installation_key, node] : *bots) {
    const auto installation_id = std::string{installation_key.str()};
    const auto path = "bots." + installation_id;
    const auto *table = node.as_table();
    if (table == nullptr || installation_id.empty()) {
      bot_configuration_error("invalid_bot_configuration", path,
                              path + " must be an installation table");
    }
    static const std::unordered_set<std::string_view> bot_keys = {
        "enabled", "surface", "transport", "connection"};
    validate_keys(*table, bot_keys, path, true);

    BotInstallationConfig config;
    config.installation_id = installation_id;
    config.enabled = required_bool(*table, "enabled", path);
    config.surface = parse_surface(required_string(*table, "surface", path),
                                   path + ".surface");
    config.transport = parse_transport(
        required_string(*table, "transport", path), path + ".transport");
    const auto *connection = table->get_as<toml::table>("connection");
    if (connection == nullptr) {
      bot_configuration_error("invalid_bot_configuration", path + ".connection",
                              path + ".connection must be a table");
    }
    const auto connection_path = path + ".connection";
    if (config.surface == BotInstallationSurface::OneBot11Qq &&
        config.transport == BotTransport::WebSocket) {
      config.connection =
          parse_onebot_websocket_connection(*connection, connection_path);
    } else if (config.surface == BotInstallationSurface::OneBot11Qq &&
               config.transport == BotTransport::Http) {
      config.connection =
          parse_onebot_http_connection(*connection, connection_path);
    } else if (config.surface == BotInstallationSurface::TelegramBotApi &&
               config.transport == BotTransport::Http) {
      config.connection =
          parse_telegram_http_connection(*connection, connection_path);
    } else {
      bot_configuration_error(
          "unsupported_bot_surface_transport", path + ".transport",
          path + " selects an unsupported surface/transport combination");
    }
    configurations.push_back(std::move(config));
  }
  return configurations;
}

auto get_string_array(const toml::node *node) -> std::vector<std::string> {
  std::vector<std::string> values;

  if (node == nullptr) {
    return values;
  }

  if (const auto array = node->as_array()) {
    for (const auto &item : *array) {
      if (auto item_str = item.value<std::string>()) {
        values.push_back(*item_str);
      }
    }
  }

  return values;
}

auto get_string_or_array(const toml::node *node) -> std::vector<std::string> {
  if (node == nullptr) {
    return {};
  }

  if (auto value = node->value<std::string>()) {
    return {*value};
  }

  return get_string_array(node);
}

auto valid_command_name(const std::string &name) -> bool {
  if (name.empty() || name.size() > 32) {
    return false;
  }
  return std::ranges::all_of(name, [](const unsigned char character) {
    return (character >= 'a' && character <= 'z') ||
           (character >= '0' && character <= '9') || character == '_';
  });
}

auto has_stage_dependency_cycle(const PipelineConfig &pipeline) -> bool {
  std::unordered_set<std::string> stage_names;
  std::unordered_map<std::string, std::vector<std::string>> graph;
  std::unordered_map<std::string, int> in_degree;

  for (const auto &stage : pipeline.stages) {
    stage_names.insert(stage.name);
    graph[stage.name] = {};
    in_degree[stage.name] = 0;
  }

  for (const auto &stage : pipeline.stages) {
    for (const auto &dependency : stage.after) {
      if (!stage_names.contains(dependency)) {
        continue;
      }
      graph[dependency].push_back(stage.name);
      in_degree[stage.name]++;
    }
  }

  std::queue<std::string> ready;
  for (const auto &[stage_name, degree] : in_degree) {
    if (degree == 0) {
      ready.push(stage_name);
    }
  }

  size_t visited = 0;
  while (!ready.empty()) {
    const auto current = ready.front();
    ready.pop();
    visited++;

    for (const auto &next : graph[current]) {
      in_degree[next]--;
      if (in_degree[next] == 0) {
        ready.push(next);
      }
    }
  }

  return visited != stage_names.size();
}

auto has_actor_dependency_cycle(const std::vector<ActorConfig> &actors)
    -> bool {
  std::unordered_set<std::string> enabled;
  std::unordered_map<std::string, size_t> in_degree;
  std::unordered_map<std::string, std::vector<std::string>> dependents;
  for (const auto &actor : actors) {
    if (actor.enabled) {
      enabled.insert(actor.name);
      in_degree[actor.name] = 0;
    }
  }
  for (const auto &actor : actors) {
    if (!actor.enabled) {
      continue;
    }
    for (const auto &dependency : actor.required) {
      if (enabled.contains(dependency)) {
        dependents[dependency].push_back(actor.name);
        ++in_degree[actor.name];
      }
    }
  }

  std::queue<std::string> ready;
  for (const auto &[actor, degree] : in_degree) {
    if (degree == 0) {
      ready.push(actor);
    }
  }
  size_t visited = 0;
  while (!ready.empty()) {
    auto actor = std::move(ready.front());
    ready.pop();
    ++visited;
    for (const auto &dependent : dependents[actor]) {
      if (--in_degree[dependent] == 0) {
        ready.push(dependent);
      }
    }
  }
  return visited != enabled.size();
}

auto content_digest(const toml::node *node) -> std::string {
  std::ostringstream normalized;
  if (node == nullptr) {
    normalized << "null";
  } else {
    normalized << toml::json_formatter{*node};
  }
  const auto content = normalized.str();
  std::array<unsigned char, SHA256_DIGEST_LENGTH> digest{};
  SHA256(reinterpret_cast<const unsigned char *>(content.data()),
         content.size(), digest.data());

  std::ostringstream encoded;
  encoded << std::hex << std::setfill('0');
  for (const auto byte : digest) {
    encoded << std::setw(2) << static_cast<unsigned int>(byte);
  }
  return encoded.str();
}

} // namespace

auto ConfigLoader::build_snapshot(const std::string &config_path)
    -> RuntimeConfigBuildResult {
  try {
    auto parsed = toml::parse_file(config_path);
    (void)parse_bot_installations(parsed);
    auto snapshot = std::unique_ptr<const RuntimeConfigSnapshot>(
        new RuntimeConfigSnapshot(config_path, std::move(parsed)));
    return RuntimeConfigBuildResult{
        .snapshot = std::move(snapshot),
    };
  } catch (const BotConfigurationError &error) {
    return RuntimeConfigBuildResult{
        .diagnostic =
            ConfigLoadDiagnostic{
                .code = error.code(),
                .path = error.path(),
                .message = error.what(),
            },
    };
  } catch (const toml::parse_error &e) {
    return RuntimeConfigBuildResult{
        .diagnostic =
            ConfigLoadDiagnostic{
                .code = "config_parse_failed",
                .path = config_path,
                .line = e.source().begin.line,
                .column = e.source().begin.column,
            },
    };
  } catch (const std::exception &) {
    return RuntimeConfigBuildResult{
        .diagnostic =
            ConfigLoadDiagnostic{
                .code = "config_load_failed",
                .path = config_path,
            },
    };
  }
}

void ConfigLoader::publish_snapshot(
    std::shared_ptr<const RuntimeConfigSnapshot> snapshot) noexcept {
  active_snapshot_.store(std::move(snapshot), std::memory_order_release);
}

auto ConfigLoader::current_snapshot() const noexcept
    -> std::shared_ptr<const RuntimeConfigSnapshot> {
  return active_snapshot_.load(std::memory_order_acquire);
}

auto ConfigLoader::load_config(const std::string &config_path) -> bool {
  auto candidate = build_snapshot(config_path);
  if (candidate.diagnostic) {
    const auto &diagnostic = candidate.diagnostic.value();
    OBCX_ERROR("Config load failed [{}] path={} line={} column={}",
               diagnostic.code, diagnostic.path, diagnostic.line,
               diagnostic.column);
    return false;
  }
  publish_snapshot(std::move(candidate.snapshot));
  OBCX_INFO("Config loaded successfully from: {}", config_path);
  return true;
}

auto RuntimeConfigSnapshot::get_bot_configs() const
    -> std::vector<BotInstallationConfig> {
  return parse_bot_installations(config_data_);
}

auto RuntimeConfigSnapshot::get_actor_configs() const
    -> std::vector<ActorConfig> {
  std::vector<ActorConfig> actor_configs;

  if (auto actors_section = config_data_.get("actors")) {
    if (auto actors_table = actors_section->as_table()) {
      for (const auto &[actor_name, actor_config] : *actors_table) {
        if (auto actor_table = actor_config.as_table()) {
          ActorConfig config;
          config.name = std::string{actor_name};
          config.library = get_string_value(*actor_table, "library");
          config.enabled = get_bool_value(*actor_table, "enabled");
          config.required = get_string_array(actor_table->get("requires"));
          config.partition =
              get_string_value(*actor_table, "partition", "global");
          config.db = get_string_value(*actor_table, "db");
          config.db_namespace =
              get_string_value(*actor_table, "db_namespace", config.name);

          actor_configs.push_back(std::move(config));
        }
      }
    }
  }

  return actor_configs;
}

auto RuntimeConfigSnapshot::get_db_instance_configs() const
    -> std::vector<DbInstanceConfig> {
  std::vector<DbInstanceConfig> db_configs;

  if (auto db_section = config_data_.get("db")) {
    if (auto db_table = db_section->as_table()) {
      if (auto instances_section = db_table->get("instances")) {
        if (auto instances_table = instances_section->as_table()) {
          for (const auto &[instance_name, instance_config] :
               *instances_table) {
            if (auto instance_table = instance_config.as_table()) {
              DbInstanceConfig config;
              config.name = std::string{instance_name};
              config.type = get_string_value(*instance_table, "type");
              config.path = get_string_value(*instance_table, "path");
              config.dsn = get_string_value(*instance_table, "dsn");
              config.config = *instance_table;

              db_configs.push_back(std::move(config));
            }
          }
        }
      }
    }
  }

  return db_configs;
}

auto RuntimeConfigSnapshot::get_pipeline_configs() const
    -> std::vector<PipelineConfig> {
  std::vector<PipelineConfig> pipeline_configs;

  if (auto pipelines_section = config_data_.get("pipelines")) {
    if (auto pipelines_table = pipelines_section->as_table()) {
      for (const auto &[pipeline_name, pipeline_config] : *pipelines_table) {
        if (auto pipeline_table = pipeline_config.as_table()) {
          PipelineConfig config;
          config.name = std::string{pipeline_name};
          config.source = get_string_value(*pipeline_table, "source");

          if (auto stages_section = pipeline_table->get("stages")) {
            if (auto stages_array = stages_section->as_array()) {
              for (const auto &stage_config : *stages_array) {
                if (auto stage_table = stage_config.as_table()) {
                  PipelineStageConfig stage;
                  stage.name = get_string_value(*stage_table, "name");
                  stage.actor = get_string_value(*stage_table, "actor");
                  stage.input = get_string_value(*stage_table, "input");
                  stage.outputs =
                      get_string_or_array(stage_table->get("output"));
                  stage.after = get_string_array(stage_table->get("after"));
                  stage.mode = get_string_value(*stage_table, "mode");

                  config.stages.push_back(std::move(stage));
                }
              }
            }
          }

          pipeline_configs.push_back(std::move(config));
        }
      }
    }
  }

  return pipeline_configs;
}

auto RuntimeConfigSnapshot::get_actor_runtime_config() const
    -> ActorRuntimeConfig {
  ActorRuntimeConfig config;

  const auto *runtime = config_data_.get_as<toml::table>("actor_runtime");
  if (runtime == nullptr) {
    return config;
  }

  if (const auto *scheduler = runtime->get_as<toml::table>("scheduler")) {
    const auto policy = get_string_value(*scheduler, "policy", "stealing");
    if (policy == "sharing") {
      config.policy = ActorSchedulerPolicy::Sharing;
    }

    config.workers = get_non_negative_size(*scheduler, "workers", 0);
    config.blocking_workers =
        get_non_negative_size(*scheduler, "blocking_workers", 0);
    config.slow_resume_warning_ms =
        get_non_negative_size(*scheduler, "slow_resume_warning_ms", 10);
  }
  if (const auto *routing = runtime->get_as<toml::table>("routing")) {
    config.routing_hop_limit = get_non_negative_size(*routing, "hop_limit", 32);
  }
  if (const auto *reload = runtime->get_as<toml::table>("reload")) {
    config.reload_drain_timeout_ms = get_non_negative_size(
        *reload, "drain_timeout_ms",
        ActorRuntimeConfig::default_reload_drain_timeout_ms);
  }

  return config;
}

auto RuntimeConfigSnapshot::get_command_runtime_config() const
    -> CommandRuntimeConfig {
  CommandRuntimeConfig config;
  const auto *runtime = config_data_.get_as<toml::table>("command_runtime");
  if (runtime == nullptr) {
    return config;
  }

  config.timeout_ms = get_non_negative_size(
      *runtime, "timeout_ms", CommandRuntimeConfig::default_timeout_ms);
  const auto *routes = runtime->get_as<toml::array>("routes");
  if (routes == nullptr) {
    return config;
  }
  for (const auto &route_node : *routes) {
    const auto *route = route_node.as_table();
    if (route == nullptr) {
      continue;
    }
    CommandRouteConfig parsed;
    parsed.actor = get_string_value(*route, "actor");
    parsed.commands = get_string_array(route->get("commands"));
    parsed.platforms = get_string_array(route->get("platforms"));
    parsed.bots = get_string_array(route->get("bots"));
    parsed.fallback =
        get_string_value(*route, "fallback", "continue") == "consume"
            ? CommandFallback::Consume
            : CommandFallback::Continue;
    parsed.timeout_ms = get_non_negative_size(*route, "timeout_ms", 0);
    config.routes.push_back(std::move(parsed));
  }
  return config;
}

auto RuntimeConfigSnapshot::validate_actor_runtime_config() const
    -> std::vector<ConfigValidationError> {
  std::vector<ConfigValidationError> errors;

  const auto *runtime = config_data_.get_as<toml::table>("actor_runtime");
  if (runtime != nullptr) {
    if (const auto *scheduler = runtime->get_as<toml::table>("scheduler")) {
      if (const auto *policy_node = scheduler->get("policy")) {
        const auto policy = policy_node->value<std::string>();
        if (!policy || (*policy != "stealing" && *policy != "sharing")) {
          errors.push_back(ConfigValidationError{
              .code = "invalid_actor_scheduler_policy",
              .message = "actor_runtime.scheduler.policy must be 'stealing' or "
                         "'sharing'",
              .dependency = policy.value_or("<non-string>"),
          });
        }
      }

      for (const auto key :
           {std::string_view{"workers"}, std::string_view{"blocking_workers"},
            std::string_view{"slow_resume_warning_ms"}}) {
        if (const auto *node = scheduler->get(key)) {
          const auto value = node->value<int64_t>();
          if (!value || *value < 0) {
            errors.push_back(ConfigValidationError{
                .code = key == "workers" ? "invalid_actor_worker_count"
                        : key == "blocking_workers"
                            ? "invalid_blocking_worker_count"
                            : "invalid_slow_resume_warning",
                .message = "actor runtime numeric scheduler options must be "
                           "non-negative integers",
                .dependency = std::string{key},
            });
          }
        }
      }
    }
    if (const auto *routing = runtime->get_as<toml::table>("routing")) {
      if (const auto *node = routing->get("hop_limit")) {
        const auto value = node->value<int64_t>();
        if (!value || *value <= 0) {
          errors.push_back(ConfigValidationError{
              .code = "invalid_routing_hop_limit",
              .message = "actor_runtime.routing.hop_limit must be a positive "
                         "integer",
              .dependency = "hop_limit",
          });
        }
      }
    }
    if (const auto *reload = runtime->get_as<toml::table>("reload")) {
      if (const auto *node = reload->get("drain_timeout_ms")) {
        const auto value = node->value<int64_t>();
        if (!value ||
            *value < static_cast<int64_t>(
                         ActorRuntimeConfig::min_reload_drain_timeout_ms) ||
            *value > static_cast<int64_t>(
                         ActorRuntimeConfig::max_reload_drain_timeout_ms)) {
          errors.push_back(ConfigValidationError{
              .code = "invalid_reload_drain_timeout",
              .message =
                  "actor_runtime.reload.drain_timeout_ms must be between " +
                  std::to_string(
                      ActorRuntimeConfig::min_reload_drain_timeout_ms) +
                  " and " +
                  std::to_string(
                      ActorRuntimeConfig::max_reload_drain_timeout_ms),
              .dependency = "drain_timeout_ms",
          });
        }
      }
    }
  }

  const auto *command_runtime =
      config_data_.get_as<toml::table>("command_runtime");
  if (command_runtime != nullptr) {
    if (const auto *timeout = command_runtime->get("timeout_ms")) {
      const auto value = timeout->value<int64_t>();
      if (!value ||
          *value < static_cast<int64_t>(CommandRuntimeConfig::min_timeout_ms) ||
          *value > static_cast<int64_t>(CommandRuntimeConfig::max_timeout_ms)) {
        errors.push_back(ConfigValidationError{
            .code = "invalid_command_timeout",
            .message = "command_runtime.timeout_ms must be between " +
                       std::to_string(CommandRuntimeConfig::min_timeout_ms) +
                       " and " +
                       std::to_string(CommandRuntimeConfig::max_timeout_ms),
            .dependency = "command_runtime.timeout_ms",
        });
      }
    }
    const auto *routes_node = command_runtime->get("routes");
    if (routes_node != nullptr && routes_node->as_array() == nullptr) {
      errors.push_back(ConfigValidationError{
          .code = "invalid_command_routes",
          .message = "command_runtime.routes must be an array of tables",
          .dependency = "command_runtime.routes",
      });
    }
    if (const auto *routes = command_runtime->get_as<toml::array>("routes")) {
      size_t route_index = 0;
      for (const auto &route_node : *routes) {
        const auto *route = route_node.as_table();
        const auto route_path =
            "command_runtime.routes[" + std::to_string(route_index++) + "]";
        if (route == nullptr) {
          errors.push_back(ConfigValidationError{
              .code = "invalid_command_route",
              .message = "command route must be a table",
              .dependency = route_path,
          });
          continue;
        }
        const auto actor = route->get("actor");
        if (actor == nullptr || !actor->is_string() ||
            actor->value_or<std::string>("").empty()) {
          errors.push_back(ConfigValidationError{
              .code = "invalid_command_actor",
              .message = "command route actor must be a non-empty string",
              .dependency = route_path + ".actor",
          });
        }
        for (const auto key :
             {std::string_view{"commands"}, std::string_view{"platforms"},
              std::string_view{"bots"}}) {
          const auto *node = route->get(key);
          const auto *array = node == nullptr ? nullptr : node->as_array();
          if (array == nullptr || array->empty() ||
              !std::ranges::all_of(*array, [](const auto &item) {
                if (!item.is_string()) {
                  return false;
                }
                const auto value = item.template value<std::string>();
                return value && !value->empty();
              })) {
            errors.push_back(ConfigValidationError{
                .code = "invalid_command_scope",
                .message = "command route commands, platforms, and bots must "
                           "be non-empty string arrays",
                .dependency = route_path + "." + std::string{key},
            });
          }
        }
        if (const auto *commands = route->get_as<toml::array>("commands")) {
          for (const auto &command : *commands) {
            const auto name = command.value<std::string>();
            if (name && !valid_command_name(*name)) {
              errors.push_back(ConfigValidationError{
                  .code = "invalid_command_name",
                  .message = "command route contains an invalid command name",
                  .dependency = *name,
              });
            }
          }
        }
        if (const auto *fallback = route->get("fallback")) {
          const auto value = fallback->value<std::string>();
          if (!value || (*value != "continue" && *value != "consume")) {
            errors.push_back(ConfigValidationError{
                .code = "invalid_command_fallback",
                .message =
                    "command route fallback must be 'continue' or 'consume'",
                .dependency = route_path + ".fallback",
            });
          }
        }
        if (const auto *timeout = route->get("timeout_ms")) {
          const auto value = timeout->value<int64_t>();
          if (!value ||
              *value <
                  static_cast<int64_t>(CommandRuntimeConfig::min_timeout_ms) ||
              *value >
                  static_cast<int64_t>(CommandRuntimeConfig::max_timeout_ms)) {
            errors.push_back(ConfigValidationError{
                .code = "invalid_command_timeout",
                .message = "command route timeout_ms is outside the supported "
                           "range",
                .dependency = route_path + ".timeout_ms",
            });
          }
        }
      }
    }
  }

  return errors;
}

auto RuntimeConfigSnapshot::validate_actor_pipeline_configs() const
    -> std::vector<ConfigValidationError> {
  std::vector<ConfigValidationError> errors;

  const auto actors = get_actor_configs();
  const auto pipelines = get_pipeline_configs();
  const auto db_instances = get_db_instance_configs();

  std::unordered_set<std::string> actor_names;
  for (const auto &actor : actors) {
    if (actor.enabled) {
      actor_names.insert(actor.name);
    }
  }

  for (const auto &actor : actors) {
    if (!actor.enabled) {
      continue;
    }
    for (const auto &dependency : actor.required) {
      if (!actor_names.contains(dependency)) {
        errors.push_back(ConfigValidationError{
            .code = "missing_actor_dependency",
            .message = "Actor requires another actor that is not declared "
                       "and enabled in [actors]",
            .actor = actor.name,
            .dependency = dependency,
        });
      }
    }
  }
  if (has_actor_dependency_cycle(actors)) {
    errors.push_back(ConfigValidationError{
        .code = "actor_dependency_cycle",
        .message = "Enabled actor dependency graph contains a cycle",
    });
  }

  std::unordered_set<std::string> db_instance_names;
  for (const auto &db_instance : db_instances) {
    db_instance_names.insert(db_instance.name);
  }

  std::unordered_set<std::string> routable_sources{
      "obcx::core::events::RawMessageEvent",
      "obcx::core::events::RawNoticeEvent", "ActorFailed"};
  for (const auto &pipeline : pipelines) {
    for (const auto &stage : pipeline.stages) {
      if (!stage.input.empty()) {
        routable_sources.insert(stage.input);
      }
      routable_sources.insert(stage.outputs.begin(), stage.outputs.end());
    }
  }

  for (const auto &actor : actors) {
    if (actor.enabled && !actor.db.empty() &&
        !db_instance_names.contains(actor.db)) {
      errors.push_back(ConfigValidationError{
          .code = "missing_db_instance",
          .message = "Actor references a DB instance that is not declared in "
                     "[db.instances]",
          .actor = actor.name,
          .dependency = actor.db,
      });
    }
  }

  for (const auto &pipeline : pipelines) {
    if (pipeline.source.empty() ||
        !routable_sources.contains(pipeline.source)) {
      errors.push_back(ConfigValidationError{
          .code = pipeline.source.empty() ? "invalid_pipeline_source"
                                          : "unknown_pipeline_source",
          .message = pipeline.source.empty()
                         ? "Pipeline source must not be empty"
                         : "Pipeline source must be a runtime ingress type, "
                           "a configured actor input, or a declared stage "
                           "output",
          .pipeline = pipeline.name,
          .input = pipeline.source,
      });
    }
    std::unordered_set<std::string> stage_names;
    for (const auto &stage : pipeline.stages) {
      if (!stage_names.insert(stage.name).second) {
        errors.push_back(ConfigValidationError{
            .code = "duplicate_stage_name",
            .message = "Pipeline stage names must be unique",
            .pipeline = pipeline.name,
            .stage = stage.name,
        });
      }
    }

    for (const auto &stage : pipeline.stages) {
      if (!stage.mode.empty() && stage.mode != "await" &&
          stage.mode != "async") {
        errors.push_back(ConfigValidationError{
            .code = "invalid_stage_mode",
            .message = "Pipeline stage mode must be 'await' or 'async'",
            .pipeline = pipeline.name,
            .stage = stage.name,
            .actor = stage.actor,
            .dependency = stage.mode,
        });
      }
      if (!actor_names.contains(stage.actor)) {
        errors.push_back(ConfigValidationError{
            .code = "missing_actor",
            .message = "Pipeline stage references an actor that is not "
                       "declared and enabled in [actors]",
            .pipeline = pipeline.name,
            .stage = stage.name,
            .actor = stage.actor,
        });
      }

      for (const auto &dependency : stage.after) {
        if (!stage_names.contains(dependency)) {
          errors.push_back(ConfigValidationError{
              .code = "missing_stage_dependency",
              .message = "Pipeline stage references an unknown dependency in "
                         "after",
              .pipeline = pipeline.name,
              .stage = stage.name,
              .actor = stage.actor,
              .dependency = dependency,
          });
        }
      }
    }

    if (has_stage_dependency_cycle(pipeline)) {
      errors.push_back(ConfigValidationError{
          .code = "stage_dependency_cycle",
          .message = "Pipeline stage dependency graph contains a cycle",
          .pipeline = pipeline.name,
      });
    }
  }

  return errors;
}

auto RuntimeConfigSnapshot::validate_actor_pipeline_contracts(
    const std::unordered_map<std::string, std::unordered_set<std::string>>
        &actor_inputs) const -> std::vector<ConfigValidationError> {
  std::vector<ConfigValidationError> errors;
  for (const auto &pipeline : get_pipeline_configs()) {
    for (const auto &stage : pipeline.stages) {
      const auto actor = actor_inputs.find(stage.actor);
      if (actor == actor_inputs.end()) {
        errors.push_back(ConfigValidationError{
            .code = "actor_unavailable",
            .message = "Pipeline stage actor was not loaded with a valid "
                       "input contract",
            .pipeline = pipeline.name,
            .stage = stage.name,
            .actor = stage.actor,
            .input = stage.input,
        });
        continue;
      }
      if (!actor->second.contains(stage.input)) {
        errors.push_back(ConfigValidationError{
            .code = "unsupported_actor_input",
            .message = "Pipeline stage input is absent from the actor input "
                       "contract",
            .pipeline = pipeline.name,
            .stage = stage.name,
            .actor = stage.actor,
            .input = stage.input,
        });
      }
    }
  }
  return errors;
}

auto RuntimeConfigSnapshot::get_section(
    const std::string_view section_name) const -> std::optional<toml::table> {
  const auto section = config_data_.at_path(section_name);
  if (section) {
    if (const auto *section_table = section.as_table()) {
      return *section_table;
    }
  }

  return std::nullopt;
}

auto RuntimeConfigSnapshot::get_actor_section(
    const std::string_view actor, const std::string_view section_name) const
    -> std::optional<toml::table> {
  const auto *actors = config_data_.get_as<toml::table>("actors");
  const auto *actor_table =
      actors == nullptr ? nullptr : actors->get_as<toml::table>(actor);
  const auto *actor_config = actor_table == nullptr
                                 ? nullptr
                                 : actor_table->get_as<toml::table>("config");
  if (actor_config == nullptr) {
    return std::nullopt;
  }
  if (section_name.empty()) {
    return *actor_config;
  }
  const auto section = actor_config->at_path(section_name);
  if (!section) {
    return std::nullopt;
  }
  const auto *section_table = section.as_table();
  return section_table == nullptr ? std::nullopt
                                  : std::optional<toml::table>{*section_table};
}

auto RuntimeConfigSnapshot::process_owned_fingerprint(
    RuntimeThreadFingerprintInput thread_budget) const
    -> ProcessOwnedConfigFingerprint {
  const auto *db = config_data_.get_as<toml::table>("db");
  const auto *instances =
      db == nullptr ? nullptr : db->get_as<toml::table>("instances");
  return ProcessOwnedConfigFingerprint{
      .bots = content_digest(config_data_.get("bots")),
      .database_instances = content_digest(instances),
      .thread_budget = thread_budget,
  };
}

auto changed_process_owned_domains(
    const ProcessOwnedConfigFingerprint &active,
    const ProcessOwnedConfigFingerprint &candidate)
    -> std::vector<std::string> {
  std::vector<std::string> changed;
  if (active.bots != candidate.bots) {
    changed.emplace_back("bots");
  }
  if (active.database_instances != candidate.database_instances) {
    changed.emplace_back("database_instances");
  }
  if (active.thread_budget != candidate.thread_budget) {
    changed.emplace_back("runtime_thread_budget");
  }
  return changed;
}

auto describe_process_owned_changes(
    const ProcessOwnedConfigFingerprint &active,
    const ProcessOwnedConfigFingerprint &candidate) -> std::string {
  const auto changed = changed_process_owned_domains(active, candidate);
  std::string description;
  for (const auto &domain : changed) {
    if (!description.empty()) {
      description += ',';
    }
    description += domain;
  }
  return description;
}

auto ConfigLoader::get_bot_configs() const
    -> std::vector<BotInstallationConfig> {
  const auto snapshot = current_snapshot();
  return snapshot == nullptr ? std::vector<BotInstallationConfig>{}
                             : snapshot->get_bot_configs();
}

auto ConfigLoader::get_actor_configs() const -> std::vector<ActorConfig> {
  const auto snapshot = current_snapshot();
  return snapshot == nullptr ? std::vector<ActorConfig>{}
                             : snapshot->get_actor_configs();
}

auto ConfigLoader::get_db_instance_configs() const
    -> std::vector<DbInstanceConfig> {
  const auto snapshot = current_snapshot();
  return snapshot == nullptr ? std::vector<DbInstanceConfig>{}
                             : snapshot->get_db_instance_configs();
}

auto ConfigLoader::get_pipeline_configs() const -> std::vector<PipelineConfig> {
  const auto snapshot = current_snapshot();
  return snapshot == nullptr ? std::vector<PipelineConfig>{}
                             : snapshot->get_pipeline_configs();
}

auto ConfigLoader::get_actor_runtime_config() const -> ActorRuntimeConfig {
  const auto snapshot = current_snapshot();
  return snapshot == nullptr ? ActorRuntimeConfig{}
                             : snapshot->get_actor_runtime_config();
}

auto ConfigLoader::get_command_runtime_config() const -> CommandRuntimeConfig {
  const auto snapshot = current_snapshot();
  return snapshot == nullptr ? CommandRuntimeConfig{}
                             : snapshot->get_command_runtime_config();
}

auto ConfigLoader::validate_actor_runtime_config() const
    -> std::vector<ConfigValidationError> {
  const auto snapshot = current_snapshot();
  return snapshot == nullptr ? std::vector<ConfigValidationError>{}
                             : snapshot->validate_actor_runtime_config();
}

auto ConfigLoader::validate_actor_pipeline_configs() const
    -> std::vector<ConfigValidationError> {
  const auto snapshot = current_snapshot();
  return snapshot == nullptr ? std::vector<ConfigValidationError>{}
                             : snapshot->validate_actor_pipeline_configs();
}

auto ConfigLoader::validate_actor_pipeline_contracts(
    const std::unordered_map<std::string, std::unordered_set<std::string>>
        &actor_inputs) const -> std::vector<ConfigValidationError> {
  const auto snapshot = current_snapshot();
  return snapshot == nullptr
             ? std::vector<ConfigValidationError>{}
             : snapshot->validate_actor_pipeline_contracts(actor_inputs);
}

auto ConfigLoader::get_section(const std::string &section_name) const
    -> std::optional<toml::table> {
  const auto snapshot = current_snapshot();
  return snapshot == nullptr ? std::nullopt
                             : snapshot->get_section(section_name);
}

auto ConfigLoader::reload_config() -> bool {
  const auto snapshot = current_snapshot();
  return snapshot != nullptr && load_config(snapshot->config_path());
}

auto ConfigLoader::get_config_path() const -> std::string {
  const auto snapshot = current_snapshot();
  return snapshot == nullptr ? std::string{} : snapshot->config_path();
}

} // namespace obcx::common
