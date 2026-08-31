#include "core/bot/bot_event_components.hpp"

#include "core/bot/bot_installation_assembler.hpp"
#include "core/bot/bot_transport_components.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>

#include <type_traits>
#include <utility>

namespace obcx::core {

BotEventCapability::BotEventCapability(boost::asio::any_io_executor executor,
                                       BotEventContext context)
    : executor_(std::move(executor)), context_(std::move(context)) {
  if (context_.installation_id.empty()) {
    throw BotComponentRuntimeError(
        "bot event capability requires an installation id");
  }
}

void BotEventCapability::subscribe_messages(MessageHandler handler) {
  if (!handler) {
    throw BotComponentRuntimeError("message event handler cannot be empty");
  }
  if (active()) {
    throw BotComponentRuntimeError(
        "message subscriptions must be installed before event activation");
  }
  std::scoped_lock lock(mutex_);
  message_handlers_.push_back(std::move(handler));
}

void BotEventCapability::subscribe_notices(NoticeHandler handler) {
  if (!handler) {
    throw BotComponentRuntimeError("notice event handler cannot be empty");
  }
  if (active()) {
    throw BotComponentRuntimeError(
        "notice subscriptions must be installed before event activation");
  }
  std::scoped_lock lock(mutex_);
  notice_handlers_.push_back(std::move(handler));
}

void BotEventCapability::activate() noexcept {
  active_.store(true, std::memory_order_release);
}

void BotEventCapability::close() noexcept {
  active_.store(false, std::memory_order_release);
}

void BotEventCapability::publish(const common::Event &event) const {
  if (!active()) {
    return;
  }
  std::visit(
      [this](const auto &typed_event) {
        using Event = std::decay_t<decltype(typed_event)>;
        if constexpr (std::is_same_v<Event, common::MessageEvent>) {
          std::vector<MessageHandler> handlers;
          {
            std::scoped_lock lock(mutex_);
            handlers = message_handlers_;
          }
          for (const auto &handler : handlers) {
            boost::asio::co_spawn(
                executor_,
                [handler, context = context_,
                 event = typed_event]() -> boost::asio::awaitable<void> {
                  co_await handler(context, event);
                },
                boost::asio::detached);
          }
        } else if constexpr (std::is_same_v<Event, common::NoticeEvent>) {
          std::vector<NoticeHandler> handlers;
          {
            std::scoped_lock lock(mutex_);
            handlers = notice_handlers_;
          }
          for (const auto &handler : handlers) {
            boost::asio::co_spawn(
                executor_,
                [handler, context = context_,
                 event = typed_event]() -> boost::asio::awaitable<void> {
                  co_await handler(context, event);
                },
                boost::asio::detached);
          }
        }
      },
      event);
}

OneBot11EventIngressComponent::OneBot11EventIngressComponent(
    boost::asio::any_io_executor executor, std::string installation_id)
    : events_(std::make_shared<BotEventCapability>(
          std::move(executor),
          BotEventContext{.installation_id = std::move(installation_id),
                          .surface =
                              common::BotInstallationSurface::OneBot11Qq})) {}

auto OneBot11EventIngressComponent::descriptor() const -> ComponentDescriptor {
  return {
      .id = ComponentId{"onebot11.event-ingress"},
      .provides = {CapabilityId{std::string{bot_capability_ids::events}}},
      .required =
          {CapabilityId{std::string{bot_capability_ids::onebot11_protocol}},
           CapabilityId{std::string{bot_capability_ids::onebot11_transport}}},
  };
}

void OneBot11EventIngressComponent::install_capabilities(
    CapabilityRegistry &registry) {
  registry.install(ComponentId{"onebot11.event-ingress"},
                   CapabilityId{std::string{bot_capability_ids::events}},
                   events_);
}

void OneBot11EventIngressComponent::prepare(
    const CapabilityRegistry &registry) {
  auto transport = registry.get<OneBot11Transport>(
      CapabilityId{std::string{bot_capability_ids::onebot11_transport}});
  transport->set_event_callback([events = events_](const common::Event &event) {
    events->publish(event);
  });
  events_->activate();
}

void OneBot11EventIngressComponent::start() {}
void OneBot11EventIngressComponent::stop() { events_->close(); }

TelegramEventIngressComponent::TelegramEventIngressComponent(
    boost::asio::any_io_executor executor, std::string installation_id)
    : events_(std::make_shared<BotEventCapability>(
          std::move(executor),
          BotEventContext{
              .installation_id = std::move(installation_id),
              .surface = common::BotInstallationSurface::TelegramBotApi})) {}

auto TelegramEventIngressComponent::descriptor() const -> ComponentDescriptor {
  return {
      .id = ComponentId{"telegram.event-ingress"},
      .provides = {CapabilityId{std::string{bot_capability_ids::events}}},
      .required =
          {CapabilityId{std::string{bot_capability_ids::telegram_protocol}},
           CapabilityId{std::string{bot_capability_ids::telegram_transport}}},
  };
}

void TelegramEventIngressComponent::install_capabilities(
    CapabilityRegistry &registry) {
  registry.install(ComponentId{"telegram.event-ingress"},
                   CapabilityId{std::string{bot_capability_ids::events}},
                   events_);
}

void TelegramEventIngressComponent::prepare(
    const CapabilityRegistry &registry) {
  auto transport = registry.get<TelegramTransport>(
      CapabilityId{std::string{bot_capability_ids::telegram_transport}});
  transport->set_event_callback([events = events_](const common::Event &event) {
    events->publish(event);
  });
  events_->activate();
}

void TelegramEventIngressComponent::start() {}
void TelegramEventIngressComponent::stop() { events_->close(); }

} // namespace obcx::core
