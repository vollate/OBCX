#ifndef OBCX_INCLUDE_CORE_BOT_OPERATION_COMPONENTS_HPP_
#define OBCX_INCLUDE_CORE_BOT_OPERATION_COMPONENTS_HPP_

#include "core/bot/bot_component_runtime.hpp"
#include "core/bot/bot_operation_dispatcher.hpp"
#include "core/bot/bot_transport_components.hpp"

#include <boost/asio/awaitable.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace obcx::core {

class TelegramMediaUploader {
public:
  TelegramMediaUploader() = default;
  TelegramMediaUploader(const TelegramMediaUploader &) = delete;
  auto operator=(const TelegramMediaUploader &)
      -> TelegramMediaUploader & = delete;
  TelegramMediaUploader(TelegramMediaUploader &&) = delete;
  auto operator=(TelegramMediaUploader &&) -> TelegramMediaUploader & = delete;
  virtual ~TelegramMediaUploader() = default;

  virtual auto upload(const bot::SendTelegramMediaGroupUploadsRequest &request)
      -> boost::asio::awaitable<std::string> = 0;
};

class TelegramMediaUploadComponent final : public BotComponent {
public:
  TelegramMediaUploadComponent();

  [[nodiscard]] auto descriptor() const -> ComponentDescriptor override;
  void install_capabilities(CapabilityRegistry &registry) override;
  void prepare(const CapabilityRegistry &registry) override;
  void start() override;
  void stop() override;

private:
  class Uploader;
  std::shared_ptr<Uploader> uploader_;
};

class OneBot11OperationsComponent final : public BotComponent {
public:
  explicit OneBot11OperationsComponent(std::string installation_id);

  [[nodiscard]] auto descriptor() const -> ComponentDescriptor override;
  void install_capabilities(CapabilityRegistry &registry) override;
  void prepare(const CapabilityRegistry &registry) override;
  void start() override;
  void stop() override;

private:
  class Endpoint;
  std::shared_ptr<Endpoint> endpoint_;
};

class TelegramOperationsComponent final : public BotComponent {
public:
  explicit TelegramOperationsComponent(std::string installation_id);

  [[nodiscard]] auto descriptor() const -> ComponentDescriptor override;
  void install_capabilities(CapabilityRegistry &registry) override;
  void prepare(const CapabilityRegistry &registry) override;
  void start() override;
  void stop() override;

private:
  class Endpoint;
  std::shared_ptr<Endpoint> endpoint_;
};

} // namespace obcx::core

#endif // OBCX_INCLUDE_CORE_BOT_OPERATION_COMPONENTS_HPP_
