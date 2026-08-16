#include <cstdint>

extern "C" auto obcx_get_actor_abi_generation() -> std::uint32_t { return 2; }

extern "C" auto obcx_get_actor_name_v2() -> const char * {
  return "missing_v2_factory";
}

extern "C" auto obcx_get_actor_version_v2() -> const char * { return "2.0"; }
