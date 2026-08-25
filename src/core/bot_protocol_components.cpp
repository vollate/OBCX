#include "core/bot_protocol_components.hpp"

#include "core/bot_installation_assembler.hpp"

#include <string>

namespace obcx::core {

OneBot11ProtocolComponent::OneBot11ProtocolComponent()
    : protocol_(std::make_shared<adapter::onebot11::ProtocolAdapter>()) {}

auto OneBot11ProtocolComponent::descriptor() const -> ComponentDescriptor {
  return {
      .id = ComponentId{"onebot11.protocol"},
      .provides = {CapabilityId{
          std::string{bot_capability_ids::onebot11_protocol}}},
      .required = {},
  };
}

void OneBot11ProtocolComponent::install_capabilities(
    CapabilityRegistry &registry) {
  registry.install(
      ComponentId{"onebot11.protocol"},
      CapabilityId{std::string{bot_capability_ids::onebot11_protocol}},
      protocol_);
}

void OneBot11ProtocolComponent::prepare(const CapabilityRegistry &) {}
void OneBot11ProtocolComponent::start() {}
void OneBot11ProtocolComponent::stop() {}

TelegramProtocolComponent::TelegramProtocolComponent()
    : protocol_(std::make_shared<adapter::telegram::ProtocolAdapter>()) {}

auto TelegramProtocolComponent::descriptor() const -> ComponentDescriptor {
  return {
      .id = ComponentId{"telegram.protocol"},
      .provides = {CapabilityId{
          std::string{bot_capability_ids::telegram_protocol}}},
      .required = {},
  };
}

void TelegramProtocolComponent::install_capabilities(
    CapabilityRegistry &registry) {
  registry.install(
      ComponentId{"telegram.protocol"},
      CapabilityId{std::string{bot_capability_ids::telegram_protocol}},
      protocol_);
}

void TelegramProtocolComponent::prepare(const CapabilityRegistry &) {}
void TelegramProtocolComponent::start() {}
void TelegramProtocolComponent::stop() {}

} // namespace obcx::core
