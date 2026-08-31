#ifndef OBCX_INCLUDE_CORE_BOT_TRANSPORT_COMPONENTS_HPP_
#define OBCX_INCLUDE_CORE_BOT_TRANSPORT_COMPONENTS_HPP_

#include "common/config_loader.hpp"
#include "common/message_type.hpp"
#include "core/bot/bot_component_runtime.hpp"
#include "onebot11/adapter/protocol_adapter.hpp"
#include "telegram/adapter/protocol_adapter.hpp"
#include "telegram/provider_types.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace obcx::core {

class OneBot11Transport {
public:
  using EventCallback = std::function<void(const common::Event &)>;

  OneBot11Transport() = default;
  OneBot11Transport(const OneBot11Transport &) = delete;
  auto operator=(const OneBot11Transport &) -> OneBot11Transport & = delete;
  OneBot11Transport(OneBot11Transport &&) = delete;
  auto operator=(OneBot11Transport &&) -> OneBot11Transport & = delete;
  virtual ~OneBot11Transport() = default;

  virtual void set_event_callback(EventCallback callback) = 0;
  [[nodiscard]] virtual auto is_connected() const -> bool = 0;
  virtual auto send_action(std::string payload, std::uint64_t echo)
      -> boost::asio::awaitable<std::string> = 0;
};

class OneBot11TransportCapability final : public OneBot11Transport {
public:
  using Config = std::variant<common::OneBot11WebSocketConnectionConfig,
                              common::OneBot11HttpConnectionConfig>;

  OneBot11TransportCapability(boost::asio::io_context &io_context,
                              Config config);
  ~OneBot11TransportCapability() override;

  OneBot11TransportCapability(const OneBot11TransportCapability &) = delete;
  auto operator=(const OneBot11TransportCapability &)
      -> OneBot11TransportCapability & = delete;
  OneBot11TransportCapability(OneBot11TransportCapability &&) = delete;
  auto operator=(OneBot11TransportCapability &&)
      -> OneBot11TransportCapability & = delete;

  void configure(std::shared_ptr<adapter::onebot11::ProtocolAdapter> protocol);
  void start();
  void stop() noexcept;
  void set_event_callback(EventCallback callback) override;
  [[nodiscard]] auto is_connected() const -> bool override;
  auto send_action(std::string payload, std::uint64_t echo)
      -> boost::asio::awaitable<std::string> override;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

class TelegramTransport {
public:
  using EventCallback = std::function<void(const common::Event &)>;

  TelegramTransport() = default;
  TelegramTransport(const TelegramTransport &) = delete;
  auto operator=(const TelegramTransport &) -> TelegramTransport & = delete;
  TelegramTransport(TelegramTransport &&) = delete;
  auto operator=(TelegramTransport &&) -> TelegramTransport & = delete;
  virtual ~TelegramTransport() = default;

  virtual void set_event_callback(EventCallback callback) = 0;
  [[nodiscard]] virtual auto is_connected() const -> bool = 0;
  virtual auto send_action(std::string payload, std::uint64_t echo)
      -> boost::asio::awaitable<std::string> = 0;
  virtual auto download_file(std::string file_id)
      -> boost::asio::awaitable<std::string> = 0;
  virtual auto download_file_content(std::string_view url,
                                     std::size_t maximum_bytes)
      -> boost::asio::awaitable<std::string> = 0;
  virtual auto upload_media_group(
      std::string_view chat_id, const std::vector<TelegramMediaUpload> &media,
      std::string_view caption, std::optional<std::int64_t> topic_id,
      std::optional<std::string> reply_to_message_id,
      const std::vector<TelegramTextEntity> &caption_entities)
      -> boost::asio::awaitable<std::string> = 0;
};

class TelegramTransportCapability final : public TelegramTransport {
public:
  TelegramTransportCapability(boost::asio::io_context &io_context,
                              common::TelegramHttpConnectionConfig config);
  ~TelegramTransportCapability() override;

  TelegramTransportCapability(const TelegramTransportCapability &) = delete;
  auto operator=(const TelegramTransportCapability &)
      -> TelegramTransportCapability & = delete;
  TelegramTransportCapability(TelegramTransportCapability &&) = delete;
  auto operator=(TelegramTransportCapability &&)
      -> TelegramTransportCapability & = delete;

  void configure(std::shared_ptr<adapter::telegram::ProtocolAdapter> protocol);
  void start();
  void stop() noexcept;
  void set_event_callback(EventCallback callback) override;
  [[nodiscard]] auto is_connected() const -> bool override;
  auto send_action(std::string payload, std::uint64_t echo)
      -> boost::asio::awaitable<std::string> override;
  auto download_file(std::string file_id)
      -> boost::asio::awaitable<std::string> override;
  auto download_file_content(std::string_view url, std::size_t maximum_bytes)
      -> boost::asio::awaitable<std::string> override;
  auto upload_media_group(
      std::string_view chat_id, const std::vector<TelegramMediaUpload> &media,
      std::string_view caption, std::optional<std::int64_t> topic_id,
      std::optional<std::string> reply_to_message_id,
      const std::vector<TelegramTextEntity> &caption_entities)
      -> boost::asio::awaitable<std::string> override;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

class OneBot11WebSocketTransportComponent final : public BotComponent {
public:
  OneBot11WebSocketTransportComponent(
      boost::asio::io_context &io_context,
      common::OneBot11WebSocketConnectionConfig config);

  [[nodiscard]] auto descriptor() const -> ComponentDescriptor override;
  void install_capabilities(CapabilityRegistry &registry) override;
  void prepare(const CapabilityRegistry &registry) override;
  void start() override;
  void stop() override;

private:
  std::shared_ptr<OneBot11TransportCapability> transport_;
};

class OneBot11HttpTransportComponent final : public BotComponent {
public:
  OneBot11HttpTransportComponent(boost::asio::io_context &io_context,
                                 common::OneBot11HttpConnectionConfig config);

  [[nodiscard]] auto descriptor() const -> ComponentDescriptor override;
  void install_capabilities(CapabilityRegistry &registry) override;
  void prepare(const CapabilityRegistry &registry) override;
  void start() override;
  void stop() override;

private:
  std::shared_ptr<OneBot11TransportCapability> transport_;
};

class TelegramHttpTransportComponent final : public BotComponent {
public:
  TelegramHttpTransportComponent(boost::asio::io_context &io_context,
                                 common::TelegramHttpConnectionConfig config);

  [[nodiscard]] auto descriptor() const -> ComponentDescriptor override;
  void install_capabilities(CapabilityRegistry &registry) override;
  void prepare(const CapabilityRegistry &registry) override;
  void start() override;
  void stop() override;

private:
  std::shared_ptr<TelegramTransportCapability> transport_;
};

} // namespace obcx::core

#endif // OBCX_INCLUDE_CORE_BOT_TRANSPORT_COMPONENTS_HPP_
