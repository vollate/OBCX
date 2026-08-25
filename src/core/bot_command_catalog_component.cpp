#include "core/bot_command_catalog_component.hpp"

#include "core/bot_installation_assembler.hpp"
#include "core/bot_operation_response_parser.hpp"
#include "core/bot_transport_components.hpp"
#include "telegram/adapter/protocol_adapter.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>

namespace obcx::core {

class TelegramCommandCatalogComponent::Catalog final
    : public TelegramCommandCatalog {
public:
  void configure(std::shared_ptr<TelegramTransport> transport) {
    if (transport == nullptr || transport_ != nullptr) {
      throw BotComponentRuntimeError(
          "Telegram command catalog requires one transport");
    }
    transport_ = std::move(transport);
  }

  auto publish(const std::vector<CommandCatalogEntry> &catalog)
      -> boost::asio::awaitable<CommandCatalogPublishResult> override {
    if (transport_ == nullptr) {
      throw BotComponentRuntimeError(
          "Telegram command catalog is not prepared");
    }
    const auto echo = echo_.fetch_add(1, std::memory_order_relaxed);
    nlohmann::json request{{"method", "setMyCommands"},
                           {"commands", nlohmann::json::array()},
                           {"echo", echo}};
    for (const auto &entry : catalog) {
      request["commands"].push_back(
          {{"command", entry.name}, {"description", entry.description}});
    }
    const auto response =
        co_await transport_->send_action(request.dump(), echo);
    const auto parsed = parse_telegram_operation_response(response, true);
    if (!parsed.ok() || !parsed.value->is_boolean() ||
        !parsed.value->get<bool>()) {
      co_return CommandCatalogPublishResult{
          .supported = true,
          .succeeded = false,
          .code = "command_catalog_publish_failed",
          .message = "Telegram command catalog publication failed",
      };
    }
    co_return CommandCatalogPublishResult{
        .supported = true,
        .succeeded = true,
    };
  }

private:
  std::shared_ptr<TelegramTransport> transport_;
  std::atomic_uint64_t echo_{};
};

TelegramCommandCatalogComponent::TelegramCommandCatalogComponent()
    : catalog_(std::make_shared<Catalog>()) {}

auto TelegramCommandCatalogComponent::descriptor() const
    -> ComponentDescriptor {
  return {
      .id = ComponentId{"telegram.command-catalog"},
      .provides = {CapabilityId{
          std::string{bot_capability_ids::telegram_command_catalog}}},
      .required =
          {CapabilityId{std::string{bot_capability_ids::telegram_protocol}},
           CapabilityId{std::string{bot_capability_ids::telegram_transport}}},
  };
}

void TelegramCommandCatalogComponent::install_capabilities(
    CapabilityRegistry &registry) {
  registry.install<TelegramCommandCatalog>(
      ComponentId{"telegram.command-catalog"},
      CapabilityId{std::string{bot_capability_ids::telegram_command_catalog}},
      catalog_);
}

void TelegramCommandCatalogComponent::prepare(
    const CapabilityRegistry &registry) {
  (void)registry.get<adapter::telegram::ProtocolAdapter>(
      CapabilityId{std::string{bot_capability_ids::telegram_protocol}});
  catalog_->configure(registry.get<TelegramTransport>(
      CapabilityId{std::string{bot_capability_ids::telegram_transport}}));
}

void TelegramCommandCatalogComponent::start() {}
void TelegramCommandCatalogComponent::stop() {}

} // namespace obcx::core
