#include "core/actor_manager.hpp"

#include "common/logger.hpp"
#include "core/command_matcher.hpp"

#include <algorithm>
#include <cctype>
#include <dlfcn.h>
#include <filesystem>
#include <limits>
#include <utility>

namespace obcx::core {

namespace {

auto same_actor_path(const std::string &left, const std::string &right)
    -> bool {
  std::error_code error;
  if (std::filesystem::equivalent(left, right, error) && !error) {
    return true;
  }
  error.clear();
  const auto canonical_left = std::filesystem::weakly_canonical(left, error);
  if (error) {
    return false;
  }
  error.clear();
  const auto canonical_right = std::filesystem::weakly_canonical(right, error);
  return !error && canonical_left == canonical_right;
}

auto is_canonical_actor_input(const std::string &name) -> bool {
  if (name.empty()) {
    return false;
  }
  size_t start = 0;
  while (start < name.size()) {
    const auto separator = name.find("::", start);
    const auto end = separator == std::string::npos ? name.size() : separator;
    if (end == start) {
      return false;
    }
    const auto first = static_cast<unsigned char>(name[start]);
    if (!(std::isalpha(first) || name[start] == '_')) {
      return false;
    }
    for (size_t index = start + 1; index < end; ++index) {
      const auto character = static_cast<unsigned char>(name[index]);
      if (!(std::isalnum(character) || name[index] == '_')) {
        return false;
      }
    }
    if (separator == std::string::npos) {
      return true;
    }
    start = separator + 2;
  }
  return false;
}

auto is_canonical_command_name(const std::string &name) -> bool {
  if (name.empty() || name.size() > 32) {
    return false;
  }
  return std::ranges::all_of(name, [](const unsigned char character) {
    return (character >= 'a' && character <= 'z') ||
           (character >= '0' && character <= '9') || character == '_';
  });
}

auto contract_integer(const common::json &value)
    -> std::optional<std::int64_t> {
  if (value.is_number_unsigned()) {
    const auto number = value.get<std::uint64_t>();
    if (number >
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
      return std::nullopt;
    }
    return static_cast<std::int64_t>(number);
  }
  if (!value.is_number_integer()) {
    return std::nullopt;
  }
  return value.get<std::int64_t>();
}

auto parse_actor_contract(const char *document,
                          const std::string &exported_name, std::string &error)
    -> std::optional<ActorInputContract> {
  if (document == nullptr) {
    error = "obcx_get_actor_contract returned nullptr";
    return std::nullopt;
  }

  common::json parsed;
  try {
    parsed = common::json::parse(document);
  } catch (...) {
    error = "actor input contract is not valid JSON";
    return std::nullopt;
  }
  if (!parsed.is_object()) {
    error = "actor input contract must be a JSON object";
    return std::nullopt;
  }
  if (parsed.contains("outputs")) {
    error = "actor input contract must not declare outputs";
    return std::nullopt;
  }
  if (!parsed.contains("schema_version") ||
      (!parsed["schema_version"].is_number_unsigned() &&
       !parsed["schema_version"].is_number_integer())) {
    error = "actor input contract schema_version must be a positive integer";
    return std::nullopt;
  }
  const auto signed_schema_version =
      parsed["schema_version"].get<std::int64_t>();
  if (signed_schema_version <= 0) {
    error = "actor input contract schema_version must be a positive integer";
    return std::nullopt;
  }
  const auto schema_version = static_cast<std::uint32_t>(signed_schema_version);
  if (schema_version != 1) {
    error = "unsupported actor input contract schema_version " +
            std::to_string(schema_version);
    return std::nullopt;
  }
  if (!parsed.contains("actor") || !parsed["actor"].is_string()) {
    error = "actor input contract actor must be a string";
    return std::nullopt;
  }
  const auto actor = parsed["actor"].get<std::string>();
  if (actor.empty() || actor != exported_name) {
    error = "actor input contract identity '" + actor +
            "' does not match exported actor name '" + exported_name + "'";
    return std::nullopt;
  }
  if (!parsed.contains("accepted_inputs") ||
      !parsed["accepted_inputs"].is_array()) {
    error = "actor input contract accepted_inputs must be an array";
    return std::nullopt;
  }

  ActorInputContract contract;
  contract.schema_version = schema_version;
  contract.actor = actor;
  for (const auto &input : parsed["accepted_inputs"]) {
    if (!input.is_string()) {
      error = "actor input contract accepted_inputs must contain strings";
      return std::nullopt;
    }
    auto canonical = input.get<std::string>();
    if (!is_canonical_actor_input(canonical)) {
      error = "actor input contract contains a malformed canonical input '" +
              canonical + "'";
      return std::nullopt;
    }
    if (!contract.accepted_input_set.insert(canonical).second) {
      error =
          "actor input contract contains duplicate input '" + canonical + "'";
      return std::nullopt;
    }
    contract.accepted_inputs.push_back(std::move(canonical));
  }
  if (contract.accepted_inputs.empty()) {
    error = "actor input contract must contain at least one accepted input";
    return std::nullopt;
  }
  if (!std::ranges::is_sorted(contract.accepted_inputs)) {
    error = "actor input contract accepted_inputs must be deterministic and "
            "sorted";
    return std::nullopt;
  }

  if (parsed.contains("commands")) {
    const auto &commands = parsed["commands"];
    if (!commands.is_array()) {
      error = "actor input contract commands must be an array";
      return std::nullopt;
    }
    std::string previous_name;
    for (const auto &entry : commands) {
      if (!entry.is_object()) {
        error = "actor input contract commands must contain objects";
        return std::nullopt;
      }
      for (const auto &[key, value] : entry.items()) {
        (void)value;
        if (key != "name" && key != "description" && key != "request_type" &&
            key != "matcher") {
          error =
              "actor command registration contains an unsupported member '" +
              key + "'";
          return std::nullopt;
        }
      }
      if (!entry.contains("name") || !entry["name"].is_string() ||
          !entry.contains("description") || !entry["description"].is_string() ||
          !entry.contains("request_type") ||
          !entry["request_type"].is_string()) {
        error = "actor command registration requires string name, "
                "description, and request_type";
        return std::nullopt;
      }
      ActorCommandRegistration registration{
          .name = entry["name"].get<std::string>(),
          .description = entry["description"].get<std::string>(),
          .request_type = entry["request_type"].get<std::string>(),
      };
      if (entry.contains("matcher")) {
        const auto &matcher = entry["matcher"];
        if (!matcher.is_object()) {
          error = "actor command matcher must be an object";
          return std::nullopt;
        }
        for (const auto &[key, value] : matcher.items()) {
          (void)value;
          if (key != "kind" && key != "pattern" && key != "mode") {
            error = "actor command matcher contains an unsupported member '" +
                    key + "'";
            return std::nullopt;
          }
        }
        if (!matcher.contains("kind") || !matcher["kind"].is_string() ||
            !matcher.contains("pattern") || !matcher["pattern"].is_string() ||
            !matcher.contains("mode") || !matcher["mode"].is_string()) {
          error = "actor command matcher requires string kind, pattern, and "
                  "mode";
          return std::nullopt;
        }
        ActorCommandMatcher parsed_matcher{
            .kind = matcher["kind"].get<std::string>(),
            .pattern = matcher["pattern"].get<std::string>(),
            .mode = matcher["mode"].get<std::string>(),
        };
        if (parsed_matcher.kind != "re2") {
          error = "actor command matcher kind is unsupported";
          return std::nullopt;
        }
        if (parsed_matcher.mode != "full") {
          error = "actor command matcher mode is unsupported";
          return std::nullopt;
        }
        const auto compiled = compile_command_re2(parsed_matcher.pattern);
        if (!compiled) {
          error = compiled.message;
          return std::nullopt;
        }
        registration.matcher.emplace(std::move(parsed_matcher));
      }
      if (!is_canonical_command_name(registration.name)) {
        error = "actor command registration contains invalid command name '" +
                registration.name + "'";
        return std::nullopt;
      }
      if (registration.description.empty()) {
        error = "actor command registration description must not be empty";
        return std::nullopt;
      }
      if (!is_canonical_actor_input(registration.request_type)) {
        error = "actor command registration contains malformed request type '" +
                registration.request_type + "'";
        return std::nullopt;
      }
      if (!contract.accepted_input_set.contains(registration.request_type)) {
        error = "actor command registration request type is not an accepted "
                "input: " +
                registration.request_type;
        return std::nullopt;
      }
      if (!previous_name.empty() && previous_name >= registration.name) {
        error = previous_name == registration.name
                    ? "actor input contract contains duplicate command '" +
                          registration.name + "'"
                    : "actor input contract commands must be deterministic and "
                      "sorted";
        return std::nullopt;
      }
      previous_name = registration.name;
      contract.commands.push_back(std::move(registration));
    }
  }

  if (!parsed.contains("configuration")) {
    return contract;
  }
  const auto &configuration = parsed["configuration"];
  if (!configuration.is_object()) {
    error = "actor configuration contract must be a JSON object";
    return std::nullopt;
  }
  for (const auto &[key, value] : configuration.items()) {
    (void)value;
    if (key != "integers" && key != "less_equal") {
      error = "actor configuration contract contains an unsupported member '" +
              key + "'";
      return std::nullopt;
    }
  }

  std::unordered_set<std::string> configuration_keys;
  if (configuration.contains("integers")) {
    const auto &integers = configuration["integers"];
    if (!integers.is_object()) {
      error = "actor configuration contract integers must be an object";
      return std::nullopt;
    }
    for (const auto &[key, constraint] : integers.items()) {
      if (key.empty() || !constraint.is_object() ||
          !constraint.contains("default")) {
        error = "actor integer configuration constraints require a key and "
                "default";
        return std::nullopt;
      }
      for (const auto &[member, value] : constraint.items()) {
        (void)value;
        if (member != "default" && member != "minimum" && member != "maximum") {
          error = "actor integer configuration constraint contains an "
                  "unsupported member '" +
                  member + "'";
          return std::nullopt;
        }
      }
      const auto default_value = contract_integer(constraint["default"]);
      const auto minimum = constraint.contains("minimum")
                               ? contract_integer(constraint["minimum"])
                               : std::optional<std::int64_t>{};
      const auto maximum = constraint.contains("maximum")
                               ? contract_integer(constraint["maximum"])
                               : std::optional<std::int64_t>{};
      if (!default_value ||
          (constraint.contains("minimum") && !minimum.has_value()) ||
          (constraint.contains("maximum") && !maximum.has_value()) ||
          (minimum && maximum && *minimum > *maximum) ||
          (minimum && *default_value < *minimum) ||
          (maximum && *default_value > *maximum)) {
        error = "actor integer configuration constraint is invalid for '" +
                key + "'";
        return std::nullopt;
      }
      configuration_keys.insert(key);
      contract.integer_configuration.push_back({.key = key,
                                                .default_value = *default_value,
                                                .minimum = minimum,
                                                .maximum = maximum});
    }
  }

  if (configuration.contains("less_equal")) {
    const auto &relations = configuration["less_equal"];
    if (!relations.is_array()) {
      error = "actor configuration contract less_equal must be an array";
      return std::nullopt;
    }
    for (const auto &relation : relations) {
      if (!relation.is_array() || relation.size() != 2 ||
          !relation[0].is_string() || !relation[1].is_string()) {
        error = "actor configuration contract less_equal entries must be "
                "two-element string arrays";
        return std::nullopt;
      }
      auto lesser = relation[0].get<std::string>();
      auto greater = relation[1].get<std::string>();
      if (!configuration_keys.contains(lesser) ||
          !configuration_keys.contains(greater)) {
        error = "actor configuration contract less_equal references an "
                "unknown integer constraint";
        return std::nullopt;
      }
      contract.less_equal_configuration.push_back(
          {.lesser = std::move(lesser), .greater = std::move(greater)});
    }
  }
  return contract;
}

} // namespace

struct ActorManager::DiscoveredActorLibrary {
  using CreateActor = void *(*)();
  using DestroyActor = void (*)(void *);

  void *handle = nullptr;
  CreateActor create_actor = nullptr;
  DestroyActor destroy_actor = nullptr;
  std::string name;
  std::string version;
  std::string path;
  ActorInputContract contract;

  ~DiscoveredActorLibrary() {
    if (handle != nullptr) {
      dlclose(handle);
    }
  }
};

SafeActorWrapper::SafeActorWrapper(void *actor_ptr,
                                   std::shared_ptr<void> library_lifetime,
                                   DestroyFunc destroy_func,
                                   std::string exported_name,
                                   std::string exported_version,
                                   ActorInputContract contract)
    : actor_ptr_(actor_ptr), library_lifetime_(std::move(library_lifetime)),
      destroy_func_(destroy_func), exported_name_(std::move(exported_name)),
      exported_version_(std::move(exported_version)),
      contract_(std::move(contract)) {}

SafeActorWrapper::~SafeActorWrapper() { reset(); }

SafeActorWrapper::SafeActorWrapper(SafeActorWrapper &&other) noexcept
    : actor_ptr_(other.actor_ptr_),
      library_lifetime_(std::move(other.library_lifetime_)),
      destroy_func_(other.destroy_func_),
      exported_name_(std::move(other.exported_name_)),
      exported_version_(std::move(other.exported_version_)),
      contract_(std::move(other.contract_)) {
  other.actor_ptr_ = nullptr;
  other.destroy_func_ = nullptr;
}

auto SafeActorWrapper::operator=(SafeActorWrapper &&other) noexcept
    -> SafeActorWrapper & {
  if (this != &other) {
    reset();
    actor_ptr_ = other.actor_ptr_;
    library_lifetime_ = std::move(other.library_lifetime_);
    destroy_func_ = other.destroy_func_;
    exported_name_ = std::move(other.exported_name_);
    exported_version_ = std::move(other.exported_version_);
    contract_ = std::move(other.contract_);
    other.actor_ptr_ = nullptr;
    other.destroy_func_ = nullptr;
  }
  return *this;
}

auto SafeActorWrapper::get() const -> IActorV2 * {
  return static_cast<IActorV2 *>(actor_ptr_);
}

auto SafeActorWrapper::operator->() const -> IActorV2 * { return get(); }

auto SafeActorWrapper::operator*() const -> IActorV2 & { return *get(); }

SafeActorWrapper::operator bool() const { return actor_ptr_ != nullptr; }

auto SafeActorWrapper::exported_name() const -> const std::string & {
  return exported_name_;
}

auto SafeActorWrapper::exported_version() const -> const std::string & {
  return exported_version_;
}

auto SafeActorWrapper::input_contract() const -> const ActorInputContract & {
  return contract_;
}

auto SafeActorWrapper::library_lifetime() const -> std::shared_ptr<void> {
  return library_lifetime_;
}

void SafeActorWrapper::reset() {
  if (actor_ptr_ && destroy_func_) {
    destroy_func_(actor_ptr_);
  }
  actor_ptr_ = nullptr;
  destroy_func_ = nullptr;
  library_lifetime_.reset();
}

ActorManager::ActorManager() = default;

ActorManager::~ActorManager() { unload_all_actors(); }

void ActorManager::add_actor_directory(const std::string &directory) {
  if (std::filesystem::exists(directory) &&
      std::filesystem::is_directory(directory)) {
    actor_directories_.push_back(directory);
    OBCX_INFO("Added actor directory: {}", directory);
  } else {
    OBCX_WARN("Actor directory does not exist: {}", directory);
  }
}

auto ActorManager::load_actor(const std::string &actor_name) -> bool {
  if (is_actor_loaded(actor_name)) {
    OBCX_WARN("Actor {} is already loaded", actor_name);
    return true;
  }

  if (!discover_actor(actor_name)) {
    return false;
  }
  return activate_actor(actor_name);
}

auto ActorManager::load_actor_from_path(const std::string &actor_path) -> bool {
  for (const auto &[name, loaded] : loaded_actors_) {
    (void)name;
    if (same_actor_path(loaded.path, actor_path)) {
      return true;
    }
  }
  if (!discover_actor_from_path(actor_path)) {
    return false;
  }
  for (const auto &[name, library] : discovered_actors_) {
    if (library && same_actor_path(library->path, actor_path)) {
      return activate_actor(name);
    }
  }
  last_error_ = "discovered actor path is unavailable for activation";
  return false;
}

auto ActorManager::discover_actor(const std::string &actor_name) -> bool {
  if (is_actor_loaded(actor_name) || discovered_actors_.contains(actor_name)) {
    return true;
  }
  const auto actor_path = find_actor_file(actor_name);
  if (actor_path.empty()) {
    last_error_ = "actor not found in actor directories";
    OBCX_ERROR("Actor {} not found in actor directories", actor_name);
    return false;
  }
  return discover_actor_from_path(actor_path);
}

auto ActorManager::discover_actor_from_path(const std::string &actor_path)
    -> bool {
  last_error_.clear();
  auto library = discover_actor_library(actor_path);
  if (!library) {
    return false;
  }
  const auto actor_name = library->name;
  if (actor_name.empty()) {
    last_error_ = "actor has an empty name";
    OBCX_ERROR("Actor loaded from {} has an empty name", actor_path);
    return false;
  }
  if (is_actor_loaded(actor_name) || discovered_actors_.contains(actor_name)) {
    OBCX_WARN("Actor {} is already loaded or discovered", actor_name);
    return true;
  }

  discovered_actors_[actor_name] = std::move(library);
  OBCX_INFO("Actor {} contract discovered successfully from {}", actor_name,
            actor_path);
  return true;
}

auto ActorManager::activate_actor(const std::string &actor_name) -> bool {
  const std::string stable_actor_name = actor_name;
  if (is_actor_loaded(stable_actor_name)) {
    return true;
  }
  const auto discovered = discovered_actors_.find(stable_actor_name);
  if (discovered == discovered_actors_.end() || !discovered->second) {
    last_error_ = "actor library has not been discovered";
    return false;
  }
  auto &library = *discovered->second;
  void *actor_ptr = nullptr;
  try {
    actor_ptr = library.create_actor();
  } catch (...) {
    last_error_ = "exception during actor construction";
    return false;
  }
  if (actor_ptr == nullptr) {
    last_error_ = "obcx_create_actor_v2 returned nullptr";
    return false;
  }

  auto library_lifetime =
      std::shared_ptr<void>{library.handle, [](void *handle) {
                              if (handle != nullptr) {
                                dlclose(handle);
                              }
                            }};
  library.handle = nullptr;
  std::shared_ptr<SafeActorWrapper> wrapper;
  try {
    wrapper = std::make_shared<SafeActorWrapper>(
        actor_ptr, library_lifetime, library.destroy_actor, library.name,
        library.version, library.contract);
  } catch (...) {
    library.destroy_actor(actor_ptr);
    last_error_ = "failed to allocate actor lifetime wrapper";
    return false;
  }

  LoadedActor loaded_actor;
  loaded_actor.wrapper = std::move(wrapper);
  loaded_actor.path = library.path;
  loaded_actors_[stable_actor_name] = std::move(loaded_actor);
  discovered_actors_.erase(discovered);
  OBCX_INFO("Actor {} constructed and activated successfully",
            stable_actor_name);
  return true;
}

auto ActorManager::activate_all_discovered() -> bool {
  const auto names = get_discovered_actor_names();
  return std::ranges::all_of(
      names, [this](const auto &name) { return activate_actor(name); });
}

void ActorManager::unload_actor(const std::string &actor_name) {
  const auto removed =
      loaded_actors_.erase(actor_name) + discovered_actors_.erase(actor_name);
  if (removed > 0) {
    OBCX_INFO("Actor {} unloaded", actor_name);
  }
}

void ActorManager::unload_all_actors() {
  begin_actor_retirement();
  finish_actor_retirement();
}

void ActorManager::begin_actor_retirement() {
  retiring_actor_libraries_.reserve(retiring_actor_libraries_.size() +
                                    loaded_actors_.size());
  for (const auto &[name, loaded] : loaded_actors_) {
    (void)name;
    if (loaded.wrapper) {
      retiring_actor_libraries_.push_back(loaded.wrapper->library_lifetime());
    }
  }
  loaded_actors_.clear();
  discovered_actors_.clear();
  OBCX_INFO("All actor instances retired");
}

void ActorManager::finish_actor_retirement() {
  retiring_actor_libraries_.clear();
  OBCX_INFO("All actors unloaded");
}

auto ActorManager::is_actor_loaded(const std::string &actor_name) const
    -> bool {
  return loaded_actors_.contains(actor_name);
}

auto ActorManager::get_actor(const std::string &actor_name) const
    -> IActorV2 * {
  if (const auto it = loaded_actors_.find(actor_name);
      it != loaded_actors_.end() && it->second.wrapper) {
    return it->second.wrapper->get();
  }
  return nullptr;
}

auto ActorManager::get_actor_shared(const std::string &actor_name) const
    -> std::shared_ptr<IActorV2> {
  if (const auto it = loaded_actors_.find(actor_name);
      it != loaded_actors_.end() && it->second.wrapper) {
    auto wrapper = it->second.wrapper;
    auto *actor = wrapper->get();
    return std::shared_ptr<IActorV2>(std::move(wrapper), actor);
  }
  return nullptr;
}

auto ActorManager::get_loaded_actor_names() const -> std::vector<std::string> {
  std::vector<std::string> names;
  names.reserve(loaded_actors_.size());
  for (const auto &[name, actor] : loaded_actors_) {
    (void)actor;
    names.push_back(name);
  }
  return names;
}

auto ActorManager::get_discovered_actor_names() const
    -> std::vector<std::string> {
  std::vector<std::string> names;
  names.reserve(discovered_actors_.size());
  for (const auto &[name, actor] : discovered_actors_) {
    (void)actor;
    names.push_back(name);
  }
  return names;
}

auto ActorManager::get_actor_contract(const std::string &actor_name) const
    -> const ActorInputContract * {
  if (const auto loaded = loaded_actors_.find(actor_name);
      loaded != loaded_actors_.end() && loaded->second.wrapper) {
    return &loaded->second.wrapper->input_contract();
  }
  if (const auto discovered = discovered_actors_.find(actor_name);
      discovered != discovered_actors_.end() && discovered->second) {
    return &discovered->second->contract;
  }
  return nullptr;
}

auto ActorManager::last_error() const -> const std::string & {
  return last_error_;
}

auto ActorManager::find_actor_file(const std::string &actor_name) const
    -> std::string {
  const std::vector possible_names = {
      actor_name,
      "lib" + actor_name + ".so",
      actor_name + ".so",
      "lib" + actor_name + ".dylib",
      actor_name + ".dylib",
      "lib" + actor_name + ".bundle",
      actor_name + ".bundle",
  };
  for (const auto &directory : actor_directories_) {
    for (const auto &name : possible_names) {
      const auto full_path = std::filesystem::path(directory) / name;
      if (std::filesystem::exists(full_path)) {
        return full_path.string();
      }
    }
  }
  return {};
}

auto ActorManager::discover_actor_library(const std::string &actor_path)
    -> std::unique_ptr<DiscoveredActorLibrary> {
  void *handle = dlopen(actor_path.c_str(), RTLD_NOW);
  if (!handle) {
    const char *error = dlerror();
    last_error_ = "failed to load actor library: " +
                  std::string{error ? error : "unknown dlopen error"};
    OBCX_ERROR("Failed to load actor library {}: {}", actor_path, last_error_);
    return nullptr;
  }

  using create_actor_t = DiscoveredActorLibrary::CreateActor;
  using destroy_actor_t = DiscoveredActorLibrary::DestroyActor;
  using actor_string_t = const char *(*)();
  using actor_abi_t = std::uint32_t (*)();

  auto close_with_error = [&](std::string error) {
    last_error_ = std::move(error);
    OBCX_ERROR("Failed to load actor library {}: {}", actor_path, last_error_);
    dlclose(handle);
  };
  auto required_symbol = [&](const char *symbol) -> void * {
    dlerror();
    void *resolved = dlsym(handle, symbol);
    if (const char *error = dlerror()) {
      close_with_error("failed to load " + std::string{symbol} +
                       " symbol: " + error);
      return nullptr;
    }
    return resolved;
  };

  auto get_actor_abi = reinterpret_cast<actor_abi_t>(
      required_symbol("obcx_get_actor_abi_generation"));
  if (!get_actor_abi) {
    return nullptr;
  }
  std::uint32_t abi_generation = 0;
  try {
    abi_generation = get_actor_abi();
  } catch (...) {
    close_with_error("actor ABI generation query threw an exception");
    return nullptr;
  }
  if (abi_generation != OBCX_ACTOR_ABI_GENERATION_V2) {
    close_with_error("unsupported actor ABI generation " +
                     std::to_string(abi_generation));
    return nullptr;
  }

  auto create_actor =
      reinterpret_cast<create_actor_t>(required_symbol("obcx_create_actor_v2"));
  if (!create_actor) {
    return nullptr;
  }
  auto destroy_actor = reinterpret_cast<destroy_actor_t>(
      required_symbol("obcx_destroy_actor_v2"));
  if (!destroy_actor) {
    return nullptr;
  }
  auto get_actor_name = reinterpret_cast<actor_string_t>(
      required_symbol("obcx_get_actor_name_v2"));
  if (!get_actor_name) {
    return nullptr;
  }
  auto get_actor_version = reinterpret_cast<actor_string_t>(
      required_symbol("obcx_get_actor_version_v2"));
  if (!get_actor_version) {
    return nullptr;
  }
  auto get_actor_contract = reinterpret_cast<actor_string_t>(
      required_symbol("obcx_get_actor_contract"));
  if (!get_actor_contract) {
    return nullptr;
  }

  std::string exported_name;
  std::string exported_version;
  const char *contract_document = nullptr;
  try {
    if (const char *name = get_actor_name()) {
      exported_name = name;
    }
    if (const char *version = get_actor_version()) {
      exported_version = version;
    }
    contract_document = get_actor_contract();
  } catch (...) {
    close_with_error("actor metadata or contract query threw an exception");
    return nullptr;
  }
  if (exported_name.empty() || exported_name == "unknown") {
    close_with_error("actor exports an empty or unknown name");
    return nullptr;
  }
  std::string contract_error;
  auto contract =
      parse_actor_contract(contract_document, exported_name, contract_error);
  if (!contract) {
    close_with_error(std::move(contract_error));
    return nullptr;
  }

  auto library = std::make_unique<DiscoveredActorLibrary>();
  library->handle = handle;
  library->create_actor = create_actor;
  library->destroy_actor = destroy_actor;
  library->name = std::move(exported_name);
  library->version = std::move(exported_version);
  library->path = actor_path;
  library->contract = std::move(*contract);
  return library;
}

} // namespace obcx::core
