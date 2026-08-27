#include "core/bot_transport_components.hpp"

#include "core/bot_installation_assembler.hpp"
#include "onebot11/network/http/connection_manager.hpp"
#include "onebot11/network/websocket/connection_manager.hpp"
#include "telegram/network/connection_manager.hpp"

#include <stdexcept>
#include <type_traits>
#include <utility>

namespace obcx::core {
namespace {

auto legacy_config(const common::OneBot11WebSocketConnectionConfig &typed)
    -> common::ConnectionConfig {
  common::ConnectionConfig config;
  config.host = typed.host;
  config.port = typed.port;
  config.access_token = typed.access_token;
  config.connect_timeout = typed.connect_timeout;
  config.action_timeout = typed.action_timeout;
  return config;
}

auto legacy_config(const common::OneBot11HttpConnectionConfig &typed)
    -> common::ConnectionConfig {
  common::ConnectionConfig config;
  config.host = typed.host;
  config.port = typed.port;
  config.access_token = typed.access_token;
  config.use_ssl = typed.use_tls;
  config.connect_timeout = typed.connect_timeout;
  config.action_timeout = typed.action_timeout;
  return config;
}

auto proxy_type_id(const common::BotProxyType type) -> std::string {
  switch (type) {
  case common::BotProxyType::Http:
    return "http";
  case common::BotProxyType::Https:
    return "https";
  case common::BotProxyType::Socks5:
    return "socks5";
  }
  throw BotComponentRuntimeError("unsupported typed proxy type");
}

auto legacy_config(const common::TelegramHttpConnectionConfig &typed)
    -> common::ConnectionConfig {
  common::ConnectionConfig config;
  config.host = typed.host;
  config.port = typed.port;
  config.access_token = typed.access_token;
  config.use_ssl = typed.use_tls;
  config.connect_timeout = typed.connect_timeout;
  config.action_timeout = typed.action_timeout;
  config.poll_timeout = typed.poll_timeout;
  config.poll_force_close = typed.poll_force_close;
  config.poll_retry_interval = typed.poll_retry_interval;
  if (typed.proxy) {
    config.proxy_host = typed.proxy->host;
    config.proxy_port = typed.proxy->port;
    config.proxy_type = proxy_type_id(typed.proxy->type);
    config.proxy_username = typed.proxy->username;
    config.proxy_password = typed.proxy->password;
  }
  return config;
}

auto transport_descriptor(std::string component_id,
                          const std::string_view protocol_capability)
    -> ComponentDescriptor {
  return {
      .id = ComponentId{std::move(component_id)},
      .provides = {CapabilityId{
          std::string{bot_capability_ids::onebot11_transport}}},
      .required = {CapabilityId{std::string{protocol_capability}}},
  };
}

} // namespace

class OneBot11TransportCapability::Impl {
public:
  using Manager =
      std::variant<std::monostate,
                   std::unique_ptr<network::WebSocketConnectionManager>,
                   std::unique_ptr<network::HttpConnectionManager>>;

  Impl(boost::asio::io_context &io_context, Config config)
      : io_context(&io_context), config(std::move(config)) {}

  boost::asio::io_context *io_context;
  Config config;
  std::shared_ptr<adapter::onebot11::ProtocolAdapter> protocol;
  Manager manager;
  EventCallback event_callback;
  bool running{};
};

OneBot11TransportCapability::OneBot11TransportCapability(
    boost::asio::io_context &io_context, Config config)
    : impl_(std::make_unique<Impl>(io_context, std::move(config))) {}

OneBot11TransportCapability::~OneBot11TransportCapability() { stop(); }

void OneBot11TransportCapability::configure(
    std::shared_ptr<adapter::onebot11::ProtocolAdapter> protocol) {
  if (protocol == nullptr ||
      !std::holds_alternative<std::monostate>(impl_->manager)) {
    throw BotComponentRuntimeError(
        "OneBot transport can only be configured once with a protocol");
  }
  impl_->protocol = std::move(protocol);
  if (const auto *websocket =
          std::get_if<common::OneBot11WebSocketConnectionConfig>(
              &impl_->config)) {
    (void)websocket;
    impl_->manager = std::make_unique<network::WebSocketConnectionManager>(
        *impl_->io_context, *impl_->protocol);
  } else {
    const auto &http =
        std::get<common::OneBot11HttpConnectionConfig>(impl_->config);
    auto manager = std::make_unique<network::HttpConnectionManager>(
        *impl_->io_context, *impl_->protocol);
    manager->set_poll_interval(http.poll_interval);
    impl_->manager = std::move(manager);
  }
  if (impl_->event_callback) {
    set_event_callback(impl_->event_callback);
  }
}

void OneBot11TransportCapability::start() {
  if (impl_->running) {
    return;
  }
  if (auto *manager =
          std::get_if<std::unique_ptr<network::WebSocketConnectionManager>>(
              &impl_->manager)) {
    (*manager)->connect(legacy_config(
        std::get<common::OneBot11WebSocketConnectionConfig>(impl_->config)));
  } else if (auto *manager =
                 std::get_if<std::unique_ptr<network::HttpConnectionManager>>(
                     &impl_->manager)) {
    (*manager)->connect(legacy_config(
        std::get<common::OneBot11HttpConnectionConfig>(impl_->config)));
  } else {
    throw BotComponentRuntimeError("OneBot transport is not configured");
  }
  impl_->running = true;
}

void OneBot11TransportCapability::stop() noexcept {
  try {
    if (auto *manager =
            std::get_if<std::unique_ptr<network::WebSocketConnectionManager>>(
                &impl_->manager)) {
      (*manager)->disconnect();
    } else if (auto *manager =
                   std::get_if<std::unique_ptr<network::HttpConnectionManager>>(
                       &impl_->manager)) {
      (*manager)->disconnect();
    }
  } catch (...) { // NOLINT(bugprone-empty-catch)
  }
  impl_->running = false;
}

void OneBot11TransportCapability::set_event_callback(EventCallback callback) {
  impl_->event_callback = std::move(callback);
  if (auto *manager =
          std::get_if<std::unique_ptr<network::WebSocketConnectionManager>>(
              &impl_->manager)) {
    (*manager)->set_event_callback(impl_->event_callback);
  } else if (auto *manager =
                 std::get_if<std::unique_ptr<network::HttpConnectionManager>>(
                     &impl_->manager)) {
    (*manager)->set_event_callback(impl_->event_callback);
  }
}

auto OneBot11TransportCapability::is_connected() const -> bool {
  if (const auto *manager =
          std::get_if<std::unique_ptr<network::WebSocketConnectionManager>>(
              &impl_->manager)) {
    return (*manager)->is_connected();
  }
  if (const auto *manager =
          std::get_if<std::unique_ptr<network::HttpConnectionManager>>(
              &impl_->manager)) {
    return (*manager)->is_connected();
  }
  return false;
}

auto OneBot11TransportCapability::send_action(std::string payload,
                                              const std::uint64_t echo)
    -> boost::asio::awaitable<std::string> {
  if (auto *manager =
          std::get_if<std::unique_ptr<network::WebSocketConnectionManager>>(
              &impl_->manager)) {
    co_return co_await (*manager)->send_action_and_wait_async(
        std::move(payload), echo);
  }
  if (auto *manager =
          std::get_if<std::unique_ptr<network::HttpConnectionManager>>(
              &impl_->manager)) {
    co_return co_await (*manager)->send_action_and_wait_async(
        std::move(payload), echo);
  }
  throw BotComponentRuntimeError("OneBot transport is not configured");
}

class TelegramTransportCapability::Impl {
public:
  Impl(boost::asio::io_context &io_context,
       common::TelegramHttpConnectionConfig config)
      : io_context(&io_context), config(std::move(config)) {}

  boost::asio::io_context *io_context;
  common::TelegramHttpConnectionConfig config;
  std::shared_ptr<adapter::telegram::ProtocolAdapter> protocol;
  std::unique_ptr<network::TelegramConnectionManager> manager;
  EventCallback event_callback;
  bool running{};
};

TelegramTransportCapability::TelegramTransportCapability(
    boost::asio::io_context &io_context,
    common::TelegramHttpConnectionConfig config)
    : impl_(std::make_unique<Impl>(io_context, std::move(config))) {}

TelegramTransportCapability::~TelegramTransportCapability() { stop(); }

void TelegramTransportCapability::configure(
    std::shared_ptr<adapter::telegram::ProtocolAdapter> protocol) {
  if (protocol == nullptr || impl_->manager != nullptr) {
    throw BotComponentRuntimeError(
        "Telegram transport can only be configured once with a protocol");
  }
  impl_->protocol = std::move(protocol);
  impl_->manager = std::make_unique<network::TelegramConnectionManager>(
      *impl_->io_context, *impl_->protocol);
  if (impl_->event_callback) {
    impl_->manager->set_event_callback(impl_->event_callback);
  }
}

void TelegramTransportCapability::start() {
  if (impl_->running) {
    return;
  }
  if (impl_->manager == nullptr) {
    throw BotComponentRuntimeError("Telegram transport is not configured");
  }
  impl_->manager->connect(legacy_config(impl_->config));
  impl_->running = true;
}

void TelegramTransportCapability::stop() noexcept {
  try {
    if (impl_->manager != nullptr) {
      impl_->manager->disconnect();
    }
  } catch (...) { // NOLINT(bugprone-empty-catch)
  }
  impl_->running = false;
}

void TelegramTransportCapability::set_event_callback(EventCallback callback) {
  impl_->event_callback = std::move(callback);
  if (impl_->manager != nullptr) {
    impl_->manager->set_event_callback(impl_->event_callback);
  }
}

auto TelegramTransportCapability::is_connected() const -> bool {
  return impl_->manager != nullptr && impl_->manager->is_connected();
}

auto TelegramTransportCapability::send_action(std::string payload,
                                              const std::uint64_t echo)
    -> boost::asio::awaitable<std::string> {
  if (impl_->manager == nullptr) {
    throw BotComponentRuntimeError("Telegram transport is not configured");
  }
  co_return co_await impl_->manager->send_action_and_wait_async(
      std::move(payload), echo);
}

auto TelegramTransportCapability::download_file(std::string file_id)
    -> boost::asio::awaitable<std::string> {
  if (impl_->manager == nullptr) {
    throw BotComponentRuntimeError("Telegram transport is not configured");
  }
  co_return co_await impl_->manager->download_file(std::move(file_id));
}

auto TelegramTransportCapability::download_file_content(
    const std::string_view url, const std::size_t maximum_bytes)
    -> boost::asio::awaitable<std::string> {
  if (impl_->manager == nullptr) {
    throw BotComponentRuntimeError("Telegram transport is not configured");
  }
  co_return co_await impl_->manager->download_file_content(url, maximum_bytes);
}

auto TelegramTransportCapability::upload_media_group(
    const std::string_view chat_id,
    const std::vector<TelegramMediaUpload> &media,
    const std::string_view caption, const std::optional<std::int64_t> topic_id,
    std::optional<std::string> reply_to_message_id,
    const std::vector<TelegramTextEntity> &caption_entities)
    -> boost::asio::awaitable<std::string> {
  if (impl_->manager == nullptr) {
    throw BotComponentRuntimeError("Telegram transport is not configured");
  }
  co_return co_await impl_->manager->upload_media_group_multipart_with_entities(
      chat_id, media, caption, topic_id, std::move(reply_to_message_id),
      caption_entities);
}

OneBot11WebSocketTransportComponent::OneBot11WebSocketTransportComponent(
    boost::asio::io_context &io_context,
    common::OneBot11WebSocketConnectionConfig config)
    : transport_(std::make_shared<OneBot11TransportCapability>(
          io_context, std::move(config))) {}

auto OneBot11WebSocketTransportComponent::descriptor() const
    -> ComponentDescriptor {
  return transport_descriptor("onebot11.transport.websocket",
                              bot_capability_ids::onebot11_protocol);
}

void OneBot11WebSocketTransportComponent::install_capabilities(
    CapabilityRegistry &registry) {
  registry.install<OneBot11Transport>(
      ComponentId{"onebot11.transport.websocket"},
      CapabilityId{std::string{bot_capability_ids::onebot11_transport}},
      transport_);
}

void OneBot11WebSocketTransportComponent::prepare(
    const CapabilityRegistry &registry) {
  transport_->configure(registry.get<adapter::onebot11::ProtocolAdapter>(
      CapabilityId{std::string{bot_capability_ids::onebot11_protocol}}));
}

void OneBot11WebSocketTransportComponent::start() { transport_->start(); }
void OneBot11WebSocketTransportComponent::stop() { transport_->stop(); }

OneBot11HttpTransportComponent::OneBot11HttpTransportComponent(
    boost::asio::io_context &io_context,
    common::OneBot11HttpConnectionConfig config)
    : transport_(std::make_shared<OneBot11TransportCapability>(
          io_context, std::move(config))) {}

auto OneBot11HttpTransportComponent::descriptor() const -> ComponentDescriptor {
  return transport_descriptor("onebot11.transport.http",
                              bot_capability_ids::onebot11_protocol);
}

void OneBot11HttpTransportComponent::install_capabilities(
    CapabilityRegistry &registry) {
  registry.install<OneBot11Transport>(
      ComponentId{"onebot11.transport.http"},
      CapabilityId{std::string{bot_capability_ids::onebot11_transport}},
      transport_);
}

void OneBot11HttpTransportComponent::prepare(
    const CapabilityRegistry &registry) {
  transport_->configure(registry.get<adapter::onebot11::ProtocolAdapter>(
      CapabilityId{std::string{bot_capability_ids::onebot11_protocol}}));
}

void OneBot11HttpTransportComponent::start() { transport_->start(); }
void OneBot11HttpTransportComponent::stop() { transport_->stop(); }

TelegramHttpTransportComponent::TelegramHttpTransportComponent(
    boost::asio::io_context &io_context,
    common::TelegramHttpConnectionConfig config)
    : transport_(std::make_shared<TelegramTransportCapability>(
          io_context, std::move(config))) {}

auto TelegramHttpTransportComponent::descriptor() const -> ComponentDescriptor {
  return {
      .id = ComponentId{"telegram.transport.http"},
      .provides = {CapabilityId{
          std::string{bot_capability_ids::telegram_transport}}},
      .required = {CapabilityId{
          std::string{bot_capability_ids::telegram_protocol}}},
  };
}

void TelegramHttpTransportComponent::install_capabilities(
    CapabilityRegistry &registry) {
  registry.install<TelegramTransport>(
      ComponentId{"telegram.transport.http"},
      CapabilityId{std::string{bot_capability_ids::telegram_transport}},
      transport_);
}

void TelegramHttpTransportComponent::prepare(
    const CapabilityRegistry &registry) {
  transport_->configure(registry.get<adapter::telegram::ProtocolAdapter>(
      CapabilityId{std::string{bot_capability_ids::telegram_protocol}}));
}

void TelegramHttpTransportComponent::start() { transport_->start(); }
void TelegramHttpTransportComponent::stop() { transport_->stop(); }

} // namespace obcx::core
