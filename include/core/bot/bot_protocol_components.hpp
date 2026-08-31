#ifndef OBCX_INCLUDE_CORE_BOT_PROTOCOL_COMPONENTS_HPP_
#define OBCX_INCLUDE_CORE_BOT_PROTOCOL_COMPONENTS_HPP_

#include "core/bot/bot_component_runtime.hpp"
#include "onebot11/adapter/protocol_adapter.hpp"
#include "telegram/adapter/protocol_adapter.hpp"

#include <memory>

namespace obcx::core {

class OneBot11ProtocolComponent final : public BotComponent {
public:
  OneBot11ProtocolComponent();

  [[nodiscard]] auto descriptor() const -> ComponentDescriptor override;
  void install_capabilities(CapabilityRegistry &registry) override;
  void prepare(const CapabilityRegistry &registry) override;
  void start() override;
  void stop() override;

private:
  std::shared_ptr<adapter::onebot11::ProtocolAdapter> protocol_;
};

class TelegramProtocolComponent final : public BotComponent {
public:
  TelegramProtocolComponent();

  [[nodiscard]] auto descriptor() const -> ComponentDescriptor override;
  void install_capabilities(CapabilityRegistry &registry) override;
  void prepare(const CapabilityRegistry &registry) override;
  void start() override;
  void stop() override;

private:
  std::shared_ptr<adapter::telegram::ProtocolAdapter> protocol_;
};

} // namespace obcx::core

#endif // OBCX_INCLUDE_CORE_BOT_PROTOCOL_COMPONENTS_HPP_
