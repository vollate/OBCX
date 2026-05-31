#include "common/embedded_locales.hpp"

#include <array>

namespace obcx::common {

// C++26 #embed expands to a comma-separated list of integer constants;
// we materialise it as an unsigned char array and expose it as std::byte
// via std::span at runtime.

#ifdef __has_embed

#if __has_embed("../../build/locales/zh_CN/LC_MESSAGES/messages.mo")
alignas(std::byte) static constexpr unsigned char zh_CN_mo_data_raw[] = {
#embed "../../build/locales/zh_CN/LC_MESSAGES/messages.mo"
};
static constexpr size_t zh_CN_mo_size = sizeof(zh_CN_mo_data_raw);
static constexpr bool has_zh_CN = true;
#else
static constexpr unsigned char zh_CN_mo_data_raw[] = {};
static constexpr size_t zh_CN_mo_size = 0;
static constexpr bool has_zh_CN = false;
#endif

#if __has_embed("../../build/locales/en_US/LC_MESSAGES/messages.mo")
alignas(std::byte) static constexpr unsigned char en_US_mo_data_raw[] = {
#embed "../../build/locales/en_US/LC_MESSAGES/messages.mo"
};
static constexpr size_t en_US_mo_size = sizeof(en_US_mo_data_raw);
static constexpr bool has_en_US = true;
#else
static constexpr unsigned char en_US_mo_data_raw[] = {};
static constexpr size_t en_US_mo_size = 0;
static constexpr bool has_en_US = false;
#endif

#else
// #embed not supported; fall back to empty payloads.
static constexpr unsigned char zh_CN_mo_data_raw[] = {};
static constexpr size_t zh_CN_mo_size = 0;
static constexpr unsigned char en_US_mo_data_raw[] = {};
static constexpr size_t en_US_mo_size = 0;
static constexpr bool has_zh_CN = false;
static constexpr bool has_en_US = false;
#endif

// Constructed at runtime — reinterpret_cast is not a constant expression.
static const std::array embedded_locale_list = {
    EmbeddedLocaleData{.locale_name = "zh_CN",
                       .data = std::span{reinterpret_cast<const std::byte *>(
                                             zh_CN_mo_data_raw),
                                         zh_CN_mo_size}},
    EmbeddedLocaleData{.locale_name = "en_US",
                       .data = std::span{reinterpret_cast<const std::byte *>(
                                             en_US_mo_data_raw),
                                         en_US_mo_size}},
};

auto get_embedded_locales() -> std::span<const EmbeddedLocaleData> {
  return embedded_locale_list;
}

} // namespace obcx::common
