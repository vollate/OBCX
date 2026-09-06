// Frozen pre-modular SDK metadata fixture. Do not include current SDK headers,
// use its export helper, or change this document to schema 2. This
// intentionally implements only the metadata/factory guard, not an old actor's
// C++ object.
#include <atomic>
#include <cstdint>

namespace {
std::atomic<unsigned> factory_calls{0};
std::atomic<unsigned> preparation_calls{0};
struct FrozenPreparationResult {
  std::uint32_t status;
  const char *message;
};
} // namespace

extern "C" auto obcx_get_actor_abi_generation() -> std::uint32_t { return 2; }
extern "C" auto obcx_get_actor_name_v2() -> const char * {
  return "frozen_schema1_actor";
}
extern "C" auto obcx_get_actor_version_v2() -> const char * { return "1.0.0"; }
extern "C" auto obcx_get_actor_contract() -> const char * {
  return R"({"schema_version":1,"actor":"frozen_schema1_actor","accepted_inputs":["obcx::tests::FrozenInput"],"commands":[],"configuration":{"bot_installations":{"target_installation":["qq","telegram"]}}})";
}
extern "C" auto obcx_create_actor_v2() -> void * {
  ++factory_calls;
  return nullptr;
}
extern "C" void obcx_destroy_actor_v2(void *) {}
extern "C" auto obcx_prepare_actor_generation_v2(void *, void *)
    -> FrozenPreparationResult {
  ++preparation_calls;
  return {1, "incompatible actor preparation must never run"};
}
extern "C" auto obcx_frozen_factory_calls() -> unsigned {
  return factory_calls.load();
}
extern "C" auto obcx_frozen_preparation_calls() -> unsigned {
  return preparation_calls.load();
}
