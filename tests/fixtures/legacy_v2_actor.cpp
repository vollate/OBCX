#include "core/actor.hpp"

#include <cstdint>
#include <string>

namespace {

class LegacyV2Actor final : public obcx::core::IActorV2 {
public:
  [[nodiscard]] auto get_name() const -> std::string override {
    return "legacy_v2_actor";
  }

  [[nodiscard]] auto get_version() const -> std::string override {
    return "2.0.0";
  }

  auto handle_message(const obcx::core::MessageEnvelope &,
                      obcx::core::ActorContext &)
      -> obcx::core::ActorTask<obcx::core::ActorResult> override {
    co_return obcx::core::ActorResult::success();
  }
};

} // namespace

extern "C" auto obcx_get_actor_abi_generation() -> std::uint32_t {
  return obcx::core::OBCX_ACTOR_ABI_GENERATION_V2;
}

extern "C" void *obcx_create_actor_v2() {
  return static_cast<obcx::core::IActorV2 *>(new LegacyV2Actor());
}

extern "C" void obcx_destroy_actor_v2(void *actor) {
  delete static_cast<obcx::core::IActorV2 *>(actor);
}

extern "C" auto obcx_get_actor_name_v2() -> const char * {
  return "legacy_v2_actor";
}

extern "C" auto obcx_get_actor_version_v2() -> const char * { return "2.0.0"; }

extern "C" auto obcx_get_actor_contract() -> const char * {
  return R"({"schema_version":1,"actor":"legacy_v2_actor","accepted_inputs":["obcx::tests::events::LegacyProbe"]})";
}
