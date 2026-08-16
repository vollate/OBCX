#ifndef OBCX_REBUILT_ACTOR_GENERATION
#error OBCX_REBUILT_ACTOR_GENERATION must be defined
#endif

extern "C" __attribute__((visibility("default"))) auto
obcx_rebuilt_actor_generation() -> int {
  return OBCX_REBUILT_ACTOR_GENERATION;
}
