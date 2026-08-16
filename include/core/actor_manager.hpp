#pragma once

#include "core/actor.hpp"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace obcx::core {

struct ActorCommandMatcher {
  std::string kind;
  std::string pattern;
  std::string mode;
};

struct ActorCommandRegistration {
  std::string name;
  std::string description;
  std::string request_type;
  std::optional<ActorCommandMatcher> matcher;
};

struct ActorInputContract {
  struct IntegerConfigurationConstraint {
    std::string key;
    std::int64_t default_value = 0;
    std::optional<std::int64_t> minimum;
    std::optional<std::int64_t> maximum;
  };

  struct LessEqualConfigurationConstraint {
    std::string lesser;
    std::string greater;
  };

  std::uint32_t schema_version = 0;
  std::string actor;
  std::vector<std::string> accepted_inputs;
  std::unordered_set<std::string> accepted_input_set;
  std::vector<ActorCommandRegistration> commands;
  std::vector<IntegerConfigurationConstraint> integer_configuration;
  std::vector<LessEqualConfigurationConstraint> less_equal_configuration;

  [[nodiscard]] auto accepts(const std::string &message_type) const -> bool {
    return accepted_input_set.contains(message_type);
  }
};

class SafeActorWrapper {
public:
  using DestroyFunc = void (*)(void *);

  SafeActorWrapper(void *actor_ptr, std::shared_ptr<void> library_lifetime,
                   DestroyFunc destroy_func, std::string exported_name,
                   std::string exported_version, ActorInputContract contract);
  ~SafeActorWrapper();

  SafeActorWrapper(const SafeActorWrapper &) = delete;
  auto operator=(const SafeActorWrapper &) -> SafeActorWrapper & = delete;

  SafeActorWrapper(SafeActorWrapper &&other) noexcept;
  auto operator=(SafeActorWrapper &&other) noexcept -> SafeActorWrapper &;

  [[nodiscard]] auto get() const -> IActorV2 *;
  auto operator->() const -> IActorV2 *;
  auto operator*() const -> IActorV2 &;
  explicit operator bool() const;

  [[nodiscard]] auto exported_name() const -> const std::string &;
  [[nodiscard]] auto exported_version() const -> const std::string &;
  [[nodiscard]] auto input_contract() const -> const ActorInputContract &;

private:
  friend class ActorManager;

  void reset();
  [[nodiscard]] auto library_lifetime() const -> std::shared_ptr<void>;

  void *actor_ptr_;
  std::shared_ptr<void> library_lifetime_;
  DestroyFunc destroy_func_;
  std::string exported_name_;
  std::string exported_version_;
  ActorInputContract contract_;
};

struct LoadedActor {
  std::shared_ptr<SafeActorWrapper> wrapper;
  std::string path;

  LoadedActor() = default;
  ~LoadedActor() = default;

  LoadedActor(const LoadedActor &) = delete;
  auto operator=(const LoadedActor &) -> LoadedActor & = delete;

  LoadedActor(LoadedActor &&other) noexcept = default;
  auto operator=(LoadedActor &&other) noexcept -> LoadedActor & = default;
};

class ActorManager {
public:
  ActorManager();
  ~ActorManager();

  ActorManager(const ActorManager &) = delete;
  auto operator=(const ActorManager &) -> ActorManager & = delete;

  void add_actor_directory(const std::string &directory);

  auto load_actor(const std::string &actor_name) -> bool;
  auto load_actor_from_path(const std::string &actor_path) -> bool;

  auto discover_actor(const std::string &actor_name) -> bool;
  auto discover_actor_from_path(const std::string &actor_path) -> bool;
  auto activate_actor(const std::string &actor_name) -> bool;
  auto activate_all_discovered() -> bool;

  void unload_actor(const std::string &actor_name);
  void unload_all_actors();

  [[nodiscard]] auto is_actor_loaded(const std::string &actor_name) const
      -> bool;
  [[nodiscard]] auto get_actor(const std::string &actor_name) const
      -> IActorV2 *;
  [[nodiscard]] auto get_actor_shared(const std::string &actor_name) const
      -> std::shared_ptr<IActorV2>;
  [[nodiscard]] auto get_loaded_actor_names() const -> std::vector<std::string>;
  [[nodiscard]] auto get_discovered_actor_names() const
      -> std::vector<std::string>;
  [[nodiscard]] auto get_actor_contract(const std::string &actor_name) const
      -> const ActorInputContract *;
  [[nodiscard]] auto last_error() const -> const std::string &;

private:
  friend class RuntimeGeneration;
  struct DiscoveredActorLibrary;

  void begin_actor_retirement();
  void finish_actor_retirement();

  [[nodiscard]] auto find_actor_file(const std::string &actor_name) const
      -> std::string;

  auto discover_actor_library(const std::string &actor_path)
      -> std::unique_ptr<DiscoveredActorLibrary>;

  std::unordered_map<std::string, LoadedActor> loaded_actors_;
  std::unordered_map<std::string, std::unique_ptr<DiscoveredActorLibrary>>
      discovered_actors_;
  std::vector<std::shared_ptr<void>> retiring_actor_libraries_;
  std::vector<std::string> actor_directories_;
  std::string last_error_;
};

} // namespace obcx::core
