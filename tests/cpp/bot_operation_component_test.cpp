#include "core/bot/bot_command_catalog_component.hpp"
#include "core/bot/bot_operation_components.hpp"
#include "core/bot/bot_protocol_components.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace asio = boost::asio;
using obcx::bot::BotAction;
using obcx::bot::BotInstallationRef;
using obcx::bot::BotSurface;
using obcx::bot::GroupTarget;
using obcx::core::BotComponent;
using obcx::core::BotOperationEndpoint;
using obcx::core::CapabilityId;
using obcx::core::CapabilityRegistry;
using obcx::core::ComponentDescriptor;
using obcx::core::ComponentId;

template <typename T> auto run(asio::awaitable<T> operation) -> T {
  asio::io_context io;
  auto future = asio::co_spawn(io, std::move(operation), asio::use_future);
  io.run();
  return future.get();
}

class FakeOneBotTransport final : public obcx::core::OneBot11Transport {
public:
  void set_event_callback(EventCallback callback) override {
    callback_ = std::move(callback);
  }
  [[nodiscard]] auto is_connected() const -> bool override { return true; }
  auto send_action(std::string payload, std::uint64_t)
      -> asio::awaitable<std::string> override {
    payloads.push_back(std::move(payload));
    if (responses.empty()) {
      throw std::runtime_error("missing OneBot response");
    }
    auto response = std::move(responses.front());
    responses.pop_front();
    co_return response;
  }

  std::deque<std::string> responses;
  std::vector<std::string> payloads;

private:
  EventCallback callback_;
};

class FakeTelegramTransport final : public obcx::core::TelegramTransport {
public:
  void set_event_callback(EventCallback callback) override {
    callback_ = std::move(callback);
  }
  [[nodiscard]] auto is_connected() const -> bool override { return true; }
  auto send_action(std::string payload, std::uint64_t)
      -> asio::awaitable<std::string> override {
    payloads.push_back(std::move(payload));
    if (responses.empty()) {
      throw std::runtime_error("missing Telegram response");
    }
    auto response = std::move(responses.front());
    responses.pop_front();
    co_return response;
  }
  auto download_file(std::string file_id)
      -> asio::awaitable<std::string> override {
    downloaded_file_id = std::move(file_id);
    co_return download_url;
  }
  auto download_file_content(std::string_view url, std::size_t maximum_bytes)
      -> asio::awaitable<std::string> override {
    downloaded_url = url;
    download_maximum_bytes = maximum_bytes;
    co_return file_content;
  }
  auto upload_media_group(
      std::string_view chat_id,
      const std::vector<obcx::core::TelegramMediaUpload> &media,
      std::string_view, std::optional<std::int64_t>, std::optional<std::string>,
      const std::vector<obcx::core::TelegramTextEntity> &)
      -> asio::awaitable<std::string> override {
    upload_chat = chat_id;
    upload_count = media.size();
    co_return upload_response;
  }

  std::deque<std::string> responses;
  std::vector<std::string> payloads;
  std::string download_url = "https://api.telegram.test/file/bot-redacted/path";
  std::string file_content = "file-bytes";
  std::string upload_response =
      R"({"ok":true,"result":[{"message_id":71},{"message_id":72}]})";
  std::string downloaded_file_id;
  std::string downloaded_url;
  std::size_t download_maximum_bytes{};
  std::string upload_chat;
  std::size_t upload_count{};

private:
  EventCallback callback_;
};

class FakeOneBotTransportComponent final : public BotComponent {
public:
  explicit FakeOneBotTransportComponent(
      std::shared_ptr<FakeOneBotTransport> transport)
      : transport_(std::move(transport)) {}

  [[nodiscard]] auto descriptor() const -> ComponentDescriptor override {
    return {.id = ComponentId{"onebot11.transport.http"},
            .provides = {CapabilityId{"onebot11.transport"}},
            .required = {CapabilityId{"onebot11.protocol"}}};
  }
  void install_capabilities(CapabilityRegistry &registry) override {
    registry.install<obcx::core::OneBot11Transport>(
        ComponentId{"onebot11.transport.http"},
        CapabilityId{"onebot11.transport"}, transport_);
  }
  void prepare(const CapabilityRegistry &) override {}
  void start() override {}
  void stop() override {}

private:
  std::shared_ptr<FakeOneBotTransport> transport_;
};

class FakeTelegramTransportComponent final : public BotComponent {
public:
  explicit FakeTelegramTransportComponent(
      std::shared_ptr<FakeTelegramTransport> transport)
      : transport_(std::move(transport)) {}

  [[nodiscard]] auto descriptor() const -> ComponentDescriptor override {
    return {.id = ComponentId{"telegram.transport.http"},
            .provides = {CapabilityId{"telegram.transport"}},
            .required = {CapabilityId{"telegram.protocol"}}};
  }
  void install_capabilities(CapabilityRegistry &registry) override {
    registry.install<obcx::core::TelegramTransport>(
        ComponentId{"telegram.transport.http"},
        CapabilityId{"telegram.transport"}, transport_);
  }
  void prepare(const CapabilityRegistry &) override {}
  void start() override {}
  void stop() override {}

private:
  std::shared_ptr<FakeTelegramTransport> transport_;
};

auto text_message() -> obcx::common::Message {
  return {{.type = "text", .data = {{"text", "hello"}}}};
}

TEST(BotOperationComponentTest,
     OneBotNativeEndpointExecutesAllDeclaredActions) {
  auto transport = std::make_shared<FakeOneBotTransport>();
  transport->responses = {
      R"({"status":"ok","retcode":0,"data":{"message_id":11}})",
      R"({"status":"ok","retcode":0,"data":null})",
      R"({"status":"ok","retcode":0,"data":{"user_id":22,"nickname":"member"}})",
      R"({"status":"ok","retcode":0,"data":{"messages":[{"sender":{"nickname":"a"},"content":"hello"}]}})",
      R"({"status":"ok","retcode":0,"data":{"url":"https://example.test/group"}})",
      R"({"status":"ok","retcode":0,"data":{"url":"https://example.test/private"}})",
      R"({"status":"ok","retcode":0,"data":null})",
  };
  obcx::core::BotInstallation installation{
      "qq-main", obcx::common::BotInstallationSurface::OneBot11Qq};
  installation.add_component(
      std::make_unique<obcx::core::OneBot11ProtocolComponent>());
  installation.add_component(
      std::make_unique<FakeOneBotTransportComponent>(transport));
  installation.add_component(
      std::make_unique<obcx::core::OneBot11OperationsComponent>("qq-main"));
  installation.start();
  const auto endpoint = installation.capability<BotOperationEndpoint>(
      CapabilityId{"bot.operations"});
  ASSERT_NE(endpoint, nullptr);
  const auto onebot_actions = endpoint->declared_actions();
  EXPECT_EQ(
      std::set<BotAction>(onebot_actions.begin(), onebot_actions.end()).size(),
      7U);

  const GroupTarget target{
      .installation = {.installation_id = "qq-main",
                       .surface = BotSurface::OneBot11Qq},
      .native_group_id = "100",
  };
  const auto sent = run(endpoint->execute(obcx::bot::SendGroupMessageRequest{
      .target = target, .message = text_message()}));
  ASSERT_TRUE(sent.ok());
  EXPECT_EQ(sent.value->primary().native_message_id, "11");
  EXPECT_TRUE(run(endpoint->execute(obcx::bot::DeleteMessageRequest{
                      .message = {.group = target, .native_message_id = "11"}}))
                  .ok());
  EXPECT_TRUE(run(endpoint->execute(obcx::bot::GetOneBotGroupMemberRequest{
                      .target = target, .user_id = "22"}))
                  .ok());
  EXPECT_TRUE(
      run(endpoint->execute(obcx::bot::GetOneBotForwardMessageRequest{
              .installation = target.installation, .forward_id = "forward"}))
          .ok());
  EXPECT_TRUE(run(endpoint->execute(obcx::bot::ResolveOneBotGroupFileRequest{
                      .target = target, .file_id = "group-file"}))
                  .ok());
  EXPECT_TRUE(run(endpoint->execute(obcx::bot::ResolveOneBotPrivateFileRequest{
                      .installation = target.installation,
                      .user_id = "22",
                      .file_id = "private-file"}))
                  .ok());
  EXPECT_TRUE(run(endpoint->execute(obcx::bot::PokeOneBotGroupRequest{
                      .target = target, .user_id = "22"}))
                  .ok());
  ASSERT_EQ(transport->payloads.size(), 7U);
  EXPECT_EQ(nlohmann::json::parse(transport->payloads.front()).at("action"),
            "send_group_msg");
}

TEST(BotOperationComponentTest,
     TelegramNativeEndpointExecutesAllCapabilitiesAndBoundsMedia) {
  auto transport = std::make_shared<FakeTelegramTransport>();
  transport->responses = {
      R"({"ok":true,"result":{"message_id":31}})",
      R"({"ok":true,"result":true})",
      R"({"ok":true,"result":{"message_id":32}})",
      R"({"ok":true,"result":{"message_id":31}})",
      R"({"ok":true,"result":{"message_id":33}})",
      R"({"ok":true,"result":[{"message_id":34},{"message_id":35}]})",
  };
  obcx::core::BotInstallation installation{
      "tg-main", obcx::common::BotInstallationSurface::TelegramBotApi};
  installation.add_component(
      std::make_unique<obcx::core::TelegramProtocolComponent>());
  installation.add_component(
      std::make_unique<FakeTelegramTransportComponent>(transport));
  installation.add_component(
      std::make_unique<obcx::core::TelegramMediaUploadComponent>());
  installation.add_component(
      std::make_unique<obcx::core::TelegramOperationsComponent>("tg-main"));
  installation.start();
  const auto endpoint = installation.capability<BotOperationEndpoint>(
      CapabilityId{"bot.operations"});
  ASSERT_NE(endpoint, nullptr);
  EXPECT_EQ(endpoint->declared_actions().size(), 8U);

  const GroupTarget target{
      .installation = {.installation_id = "tg-main",
                       .surface = BotSurface::TelegramBotApi},
      .native_group_id = "-1001",
  };
  const auto sent = run(endpoint->execute(obcx::bot::SendGroupMessageRequest{
      .target = target, .message = text_message()}));
  ASSERT_TRUE(sent.ok());
  EXPECT_EQ(sent.value->primary().native_message_id, "31");
  EXPECT_TRUE(run(endpoint->execute(obcx::bot::DeleteMessageRequest{
                      .message = {.group = target, .native_message_id = "31"}}))
                  .ok());
  EXPECT_TRUE(run(endpoint->execute(obcx::bot::SendTelegramTopicMessageRequest{
                      .target = {.group = target, .topic_id = 7},
                      .message = text_message()}))
                  .ok());
  EXPECT_TRUE(run(endpoint->execute(obcx::bot::EditTelegramMessageTextRequest{
                      .message = {.group = target, .native_message_id = "31"},
                      .text = "edited"}))
                  .ok());
  EXPECT_TRUE(
      run(endpoint->execute(obcx::bot::SendTelegramPhotoRequest{
              .target = target, .photo = "file-id", .caption = "caption"}))
          .ok());
  EXPECT_TRUE(
      run(endpoint->execute(obcx::bot::SendTelegramMediaGroupUrlsRequest{
              .target = target,
              .media = {{.type = "photo", .source = "file-a"},
                        {.type = "photo", .source = "file-b"}}}))
          .ok());
  const auto uploaded =
      run(endpoint->execute(obcx::bot::SendTelegramMediaGroupUploadsRequest{
          .target = target,
          .media = {{.type = "photo",
                     .filename = "a.jpg",
                     .mime_type = "image/jpeg",
                     .bytes = {1, 2, 3}},
                    {.type = "photo",
                     .filename = "b.jpg",
                     .mime_type = "image/jpeg",
                     .bytes = {4, 5, 6}}},
          .maximum_bytes = 16}));
  ASSERT_TRUE(uploaded.ok());
  EXPECT_EQ(transport->upload_chat, "-1001");
  EXPECT_EQ(transport->upload_count, 2U);

  auto fetched = run(endpoint->execute(
      obcx::bot::FetchTelegramFileRequest{.installation = target.installation,
                                          .file = {.file_id = "telegram-file"},
                                          .maximum_bytes = 32}));
  ASSERT_TRUE(fetched.ok());
  EXPECT_EQ(
      std::string(fetched.value->bytes.begin(), fetched.value->bytes.end()),
      "file-bytes");
  EXPECT_EQ(transport->downloaded_file_id, "telegram-file");
  EXPECT_EQ(transport->download_maximum_bytes, 32U);

  transport->file_content = std::string(33, 'x');
  fetched = run(endpoint->execute(
      obcx::bot::FetchTelegramFileRequest{.installation = target.installation,
                                          .file = {.file_id = "oversized"},
                                          .maximum_bytes = 32}));
  ASSERT_FALSE(fetched.ok());
  EXPECT_EQ(fetched.error->code,
            obcx::bot::BotOperationErrorCode::MediaTooLarge);
}

TEST(BotOperationComponentTest,
     TelegramUploadActionIsAbsentWithoutUploaderCapability) {
  auto transport = std::make_shared<FakeTelegramTransport>();
  obcx::core::BotInstallation installation{
      "tg-reduced", obcx::common::BotInstallationSurface::TelegramBotApi};
  installation.add_component(
      std::make_unique<obcx::core::TelegramProtocolComponent>());
  installation.add_component(
      std::make_unique<FakeTelegramTransportComponent>(transport));
  installation.add_component(
      std::make_unique<obcx::core::TelegramOperationsComponent>("tg-reduced"));
  installation.start();
  const auto endpoint = installation.capability<BotOperationEndpoint>(
      CapabilityId{"bot.operations"});
  const auto actions = endpoint->declared_actions();
  EXPECT_EQ(actions.size(), 7U);
  EXPECT_EQ(
      std::ranges::find(actions, BotAction::SendTelegramMediaGroupUploads),
      actions.end());
}

TEST(BotOperationComponentTest,
     TelegramSendOperationsEnforceProviderResultShapesAndCounts) {
  auto transport = std::make_shared<FakeTelegramTransport>();
  transport->responses = {
      R"({"ok":true,"result":[{"message_id":31}]})",
      R"({"ok":true,"result":[{"message_id":32}]})",
      R"({"ok":true,"result":{"message_id":33}})",
  };
  transport->upload_response = R"({"ok":true,"result":[{"message_id":34}]})";
  obcx::core::BotInstallation installation{
      "tg-shapes", obcx::common::BotInstallationSurface::TelegramBotApi};
  installation.add_component(
      std::make_unique<obcx::core::TelegramProtocolComponent>());
  installation.add_component(
      std::make_unique<FakeTelegramTransportComponent>(transport));
  installation.add_component(
      std::make_unique<obcx::core::TelegramMediaUploadComponent>());
  installation.add_component(
      std::make_unique<obcx::core::TelegramOperationsComponent>("tg-shapes"));
  installation.start();
  const auto endpoint = installation.capability<BotOperationEndpoint>(
      CapabilityId{"bot.operations"});
  const GroupTarget target{
      .installation = {.installation_id = "tg-shapes",
                       .surface = BotSurface::TelegramBotApi},
      .native_group_id = "-1001",
  };
  const auto expect_malformed = [](const auto &result) {
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error->code,
              obcx::bot::BotOperationErrorCode::MalformedResponse);
    EXPECT_EQ(result.error->submission_safety,
              obcx::bot::SubmissionSafety::PossiblySubmitted);
  };

  expect_malformed(
      run(endpoint->execute(obcx::bot::SendTelegramTopicMessageRequest{
          .target = {.group = target, .topic_id = 7},
          .message = text_message()})));
  expect_malformed(run(endpoint->execute(obcx::bot::SendTelegramPhotoRequest{
      .target = target, .photo = "file-id"})));
  expect_malformed(
      run(endpoint->execute(obcx::bot::SendTelegramMediaGroupUrlsRequest{
          .target = target,
          .media = {{.type = "photo", .source = "file-a"},
                    {.type = "photo", .source = "file-b"}}})));
  expect_malformed(
      run(endpoint->execute(obcx::bot::SendTelegramMediaGroupUploadsRequest{
          .target = target,
          .media = {{.type = "photo",
                     .filename = "a.jpg",
                     .mime_type = "image/jpeg",
                     .bytes = {1}},
                    {.type = "photo",
                     .filename = "b.jpg",
                     .mime_type = "image/jpeg",
                     .bytes = {2}}},
          .maximum_bytes = 16})));
}

TEST(BotOperationComponentTest,
     TelegramCommandCatalogUsesItsExplicitInstallationCapability) {
  auto transport = std::make_shared<FakeTelegramTransport>();
  transport->responses.push_back(R"({"ok":true,"result":true})");
  obcx::core::BotInstallation installation{
      "tg-commands", obcx::common::BotInstallationSurface::TelegramBotApi};
  installation.add_component(
      std::make_unique<obcx::core::TelegramProtocolComponent>());
  installation.add_component(
      std::make_unique<FakeTelegramTransportComponent>(transport));
  installation.add_component(
      std::make_unique<obcx::core::TelegramCommandCatalogComponent>());
  installation.start();
  const auto catalog =
      installation.capability<obcx::core::TelegramCommandCatalog>(
          CapabilityId{"telegram.command-catalog"});
  const auto result = run(
      catalog->publish({{.name = "chat", .description = "Chat with the bot"}}));
  EXPECT_TRUE(result.succeeded);
  ASSERT_EQ(transport->payloads.size(), 1U);
  const auto payload = nlohmann::json::parse(transport->payloads.front());
  EXPECT_EQ(payload.at("method"), "setMyCommands");
  EXPECT_EQ(payload.at("commands").at(0).at("command"), "chat");
}

TEST(BotOperationComponentTest,
     NativeEndpointsConservativelyRejectMalformedProviderSuccess) {
  auto transport = std::make_shared<FakeOneBotTransport>();
  transport->responses.push_back(R"({"status":"ok","retcode":0,"data":null})");
  obcx::core::BotInstallation installation{
      "qq-main", obcx::common::BotInstallationSurface::OneBot11Qq};
  installation.add_component(
      std::make_unique<obcx::core::OneBot11ProtocolComponent>());
  installation.add_component(
      std::make_unique<FakeOneBotTransportComponent>(transport));
  installation.add_component(
      std::make_unique<obcx::core::OneBot11OperationsComponent>("qq-main"));
  installation.start();
  const auto endpoint = installation.capability<BotOperationEndpoint>(
      CapabilityId{"bot.operations"});
  const GroupTarget target{
      .installation = {.installation_id = "qq-main",
                       .surface = BotSurface::OneBot11Qq},
      .native_group_id = "100",
  };
  const auto result = run(endpoint->execute(obcx::bot::SendGroupMessageRequest{
      .target = target, .message = text_message()}));
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error->code,
            obcx::bot::BotOperationErrorCode::MalformedResponse);
  EXPECT_EQ(result.error->submission_safety,
            obcx::bot::SubmissionSafety::PossiblySubmitted);
}

} // namespace
