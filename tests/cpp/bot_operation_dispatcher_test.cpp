#include "core/bot_operation_dispatcher.hpp"
#include "core/qq_bot.hpp"
#include "core/qq_telegram_bot_endpoints.hpp"
#include "core/tg_bot.hpp"
#include "network/http_client.hpp"
#include "onebot11/adapter/protocol_adapter.hpp"
#include "telegram/adapter/protocol_adapter.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace asio = boost::asio;

namespace {

using obcx::bot::BotAction;
using obcx::bot::BotInstallationRef;
using obcx::bot::BotMessageRef;
using obcx::bot::BotOperationErrorCode;
using obcx::bot::BotOperationResult;
using obcx::bot::BotSurface;
using obcx::bot::GetOneBotGroupMemberRequest;
using obcx::bot::GroupTarget;
using obcx::bot::OneBotGroupMember;
using obcx::bot::SendGroupMessageRequest;
using obcx::bot::SendMessageResult;
using obcx::bot::SubmissionSafety;

class RecordingEndpoint final : public obcx::core::BotOperationEndpoint {
public:
  RecordingEndpoint(BotInstallationRef installation,
                    std::vector<BotAction> actions)
      : installation_(std::move(installation)), actions_(std::move(actions)) {}

  [[nodiscard]] auto installation() const -> BotInstallationRef override {
    return installation_;
  }

  [[nodiscard]] auto declared_actions() const
      -> std::vector<BotAction> override {
    return actions_;
  }

  auto execute(const SendGroupMessageRequest &request)
      -> asio::awaitable<BotOperationResult<SendMessageResult>> override {
    send_calls.fetch_add(1, std::memory_order_relaxed);
    if (send_http_error_state.has_value()) {
      throw obcx::network::HttpClientError(send_exception,
                                           *send_http_error_state);
    }
    if (!send_exception.empty()) {
      throw std::runtime_error(send_exception);
    }
    co_return BotOperationResult<SendMessageResult>::success(
        {.messages = {
             {.group = request.target, .native_message_id = "message-7"}}});
  }

  auto execute(const GetOneBotGroupMemberRequest &request)
      -> asio::awaitable<BotOperationResult<OneBotGroupMember>> override {
    member_calls.fetch_add(1, std::memory_order_relaxed);
    if (!member_exception.empty()) {
      throw std::runtime_error(member_exception);
    }
    co_return BotOperationResult<OneBotGroupMember>::success(
        {.target = request.target,
         .user_id = request.user_id,
         .nickname = "member"});
  }

  std::atomic_int send_calls{};
  std::atomic_int member_calls{};
  std::string send_exception;
  std::optional<obcx::network::HttpRequestSubmissionState>
      send_http_error_state;
  std::string member_exception;

private:
  BotInstallationRef installation_;
  std::vector<BotAction> actions_;
};

class ScriptedQQBot : public obcx::core::QQBot {
public:
  ScriptedQQBot() : QQBot(obcx::adapter::onebot11::ProtocolAdapter{}) {}

  auto send_group_message(std::string_view group_id,
                          const obcx::common::Message &)
      -> asio::awaitable<std::string> override {
    last_group = group_id;
    co_return send_response;
  }

  auto delete_message(std::string_view message_id)
      -> asio::awaitable<std::string> override {
    last_delete = message_id;
    co_return delete_response;
  }

  auto get_group_member_info(std::string_view group_id,
                             std::string_view user_id, bool no_cache)
      -> asio::awaitable<std::string> override {
    last_group = group_id;
    last_user = user_id;
    last_no_cache = no_cache;
    co_return member_response;
  }

  auto get_forward_msg(std::string_view forward_id)
      -> asio::awaitable<std::string> override {
    last_forward = forward_id;
    co_return forward_response;
  }

  auto get_group_file_url(std::string_view group_id, std::string_view file_id)
      -> asio::awaitable<std::string> override {
    last_group = group_id;
    last_file = file_id;
    co_return group_file_response;
  }

  auto get_private_file_url(std::string_view user_id, std::string_view file_id)
      -> asio::awaitable<std::string> override {
    last_user = user_id;
    last_file = file_id;
    co_return private_file_response;
  }

  auto group_poke(std::string_view group_id, std::string_view user_id)
      -> asio::awaitable<std::string> override {
    last_group = group_id;
    last_user = user_id;
    co_return poke_response;
  }

  std::string send_response =
      R"({"status":"ok","retcode":0,"data":{"message_id":7001}})";
  std::string delete_response = R"({"status":"ok","retcode":0,"data":null})";
  std::string member_response =
      R"({"status":"ok","retcode":0,"data":{"user_id":456,"nickname":"nick","card":"card","title":"title"}})";
  std::string forward_response =
      R"({"status":"ok","retcode":0,"data":{"messages":[{"sender":{"nickname":"alice"},"content":"hello"}]}})";
  std::string group_file_response =
      R"({"status":"ok","retcode":0,"data":{"url":"https://example.test/group"}})";
  std::string private_file_response =
      R"({"status":"ok","retcode":0,"data":{"url":"https://example.test/private"}})";
  std::string poke_response = R"({"status":"ok","retcode":0,"data":null})";
  std::string last_group;
  std::string last_delete;
  std::string last_user;
  std::string last_forward;
  std::string last_file;
  bool last_no_cache{};
};

class ScriptedTelegramBot final : public obcx::core::TGBot {
public:
  ScriptedTelegramBot() : TGBot(obcx::adapter::telegram::ProtocolAdapter{}) {}

  auto send_group_message(std::string_view group_id,
                          const obcx::common::Message &)
      -> asio::awaitable<std::string> override {
    last_group = group_id;
    co_return send_response;
  }

  auto delete_message(std::string_view message_id)
      -> asio::awaitable<std::string> override {
    last_delete = message_id;
    co_return delete_response;
  }

  auto send_topic_message(std::string_view group_id, std::int64_t topic_id,
                          const obcx::common::Message &)
      -> asio::awaitable<std::string> override {
    last_group = group_id;
    last_topic = topic_id;
    co_return topic_response;
  }

  auto edit_message_text(std::string_view chat_id, std::string_view message_id,
                         std::string_view text, std::string_view parse_mode)
      -> asio::awaitable<std::string> override {
    last_edit_chat = chat_id;
    last_edit_message = message_id;
    last_edit_text = text;
    last_parse_mode = parse_mode;
    co_return edit_response;
  }

  auto send_group_photo_with_entities(
      std::string_view group_id, std::string_view photo,
      std::string_view caption,
      const std::vector<obcx::core::TelegramTextEntity> &entities)
      -> asio::awaitable<std::string> override {
    last_group = group_id;
    last_photo = photo;
    last_caption = caption;
    last_entities = entities;
    co_return photo_response;
  }

  auto send_media_group_with_entities(
      std::string_view chat_id,
      const std::vector<std::pair<std::string, std::string>> &media,
      std::string_view caption, std::optional<std::int64_t> topic_id,
      std::optional<std::string> reply_to_message_id,
      const std::vector<obcx::core::TelegramTextEntity> &entities)
      -> asio::awaitable<std::string> override {
    last_group = chat_id;
    last_media = media;
    last_caption = caption;
    last_topic_optional = topic_id;
    last_reply = std::move(reply_to_message_id);
    last_entities = entities;
    co_return media_response;
  }

  auto send_media_group_uploads_with_entities(
      std::string_view chat_id,
      const std::vector<obcx::core::TelegramMediaUpload> &media,
      std::string_view caption, std::optional<std::int64_t> topic_id,
      std::optional<std::string> reply_to_message_id,
      const std::vector<obcx::core::TelegramTextEntity> &entities)
      -> asio::awaitable<std::string> override {
    last_group = chat_id;
    last_uploads = media;
    last_caption = caption;
    last_topic_optional = topic_id;
    last_reply = std::move(reply_to_message_id);
    last_entities = entities;
    co_return upload_response;
  }

  auto get_media_download_url(const obcx::core::MediaFileInfo &file)
      -> asio::awaitable<std::optional<std::string>> override {
    last_file = file;
    co_return file_url;
  }

  auto download_file_content(std::string_view url)
      -> asio::awaitable<std::string> override {
    last_download_url = url;
    co_return file_content;
  }

  std::string send_response = R"({"ok":true,"result":{"message_id":8001}})";
  std::string delete_response = R"({"ok":true,"result":true})";
  std::string topic_response = R"({"ok":true,"result":{"message_id":8002}})";
  std::string edit_response = R"({"ok":true,"result":{"message_id":8001}})";
  std::string photo_response = R"({"ok":true,"result":{"message_id":8100}})";
  std::string media_response =
      R"({"ok":true,"result":[{"message_id":8101},{"message_id":8102}]})";
  std::string upload_response =
      R"({"ok":true,"result":[{"message_id":8201},{"message_id":8202}]})";
  std::optional<std::string> file_url =
      "https://api.telegram.test/file/bot-redacted/path";
  std::string file_content = "file-bytes";
  std::string last_group;
  std::string last_delete;
  std::int64_t last_topic{};
  std::string last_edit_chat;
  std::string last_edit_message;
  std::string last_edit_text;
  std::string last_parse_mode;
  std::string last_photo;
  std::string last_caption;
  std::vector<obcx::core::TelegramTextEntity> last_entities;
  std::vector<std::pair<std::string, std::string>> last_media;
  std::vector<obcx::core::TelegramMediaUpload> last_uploads;
  std::optional<std::int64_t> last_topic_optional;
  std::optional<std::string> last_reply;
  obcx::core::MediaFileInfo last_file;
  std::string last_download_url;
};

class NoUploadTelegramBot final : public ScriptedQQBot,
                                  public obcx::core::ITelegramBot {
public:
  auto send_topic_message(std::string_view, std::int64_t,
                          const obcx::common::Message &)
      -> asio::awaitable<std::string> override {
    co_return "{}";
  }
  auto send_group_photo(std::string_view, std::string_view, std::string_view)
      -> asio::awaitable<std::string> override {
    co_return "{}";
  }
  auto send_media_group(
      std::string_view,
      const std::vector<std::pair<std::string, std::string>> &,
      std::string_view, std::optional<std::int64_t>, std::optional<std::string>)
      -> asio::awaitable<std::string> override {
    co_return "{}";
  }
  auto edit_message_text(std::string_view, std::string_view, std::string_view,
                         std::string_view)
      -> asio::awaitable<std::string> override {
    co_return "{}";
  }
  auto get_media_download_url(const obcx::core::MediaFileInfo &)
      -> asio::awaitable<std::optional<std::string>> override {
    co_return std::nullopt;
  }
  auto get_media_download_urls(
      const std::vector<obcx::core::MediaFileInfo> &media)
      -> asio::awaitable<std::vector<std::optional<std::string>>> override {
    co_return std::vector<std::optional<std::string>>(media.size());
  }
  auto download_file_content(std::string_view)
      -> asio::awaitable<std::string> override {
    co_return "";
  }
};

template <typename T> auto run(asio::awaitable<T> operation) -> T {
  asio::io_context context;
  auto future = asio::co_spawn(context, std::move(operation), asio::use_future);
  context.run();
  return future.get();
}

auto text_message() -> obcx::common::Message {
  return {{.type = "text", .data = {{"text", "hello"}}}};
}

TEST(BotOperationDispatcherTest, WrapsExistingBotsByConfiguredNameAndType) {
  auto qq = std::make_shared<obcx::core::QQBot>(
      obcx::adapter::onebot11::ProtocolAdapter{});
  auto telegram = std::make_shared<obcx::core::TGBot>(
      obcx::adapter::telegram::ProtocolAdapter{});

  auto qq_endpoint =
      obcx::core::make_existing_bot_operation_endpoint("qq-main", "qq", qq);
  auto telegram_endpoint = obcx::core::make_existing_bot_operation_endpoint(
      "tg-main", "telegram", telegram);
  EXPECT_EQ(qq_endpoint->installation(),
            (BotInstallationRef{.installation_id = "qq-main",
                                .surface = BotSurface::OneBot11Qq}));
  EXPECT_EQ(telegram_endpoint->installation(),
            (BotInstallationRef{.installation_id = "tg-main",
                                .surface = BotSurface::TelegramBotApi}));

  obcx::core::QQTelegramOperationDispatcher dispatcher;
  dispatcher.register_endpoint(std::move(qq_endpoint));
  dispatcher.register_endpoint(std::move(telegram_endpoint));
  EXPECT_EQ(dispatcher.endpoint_count(), 2U);

  EXPECT_THROW((void)obcx::core::make_existing_bot_operation_endpoint(
                   "bad", "telegram", qq),
               std::invalid_argument);
  EXPECT_THROW((void)obcx::core::make_existing_bot_operation_endpoint(
                   "bad", "qq", telegram),
               std::invalid_argument);
  EXPECT_THROW((void)obcx::core::make_existing_bot_operation_endpoint(
                   "bad", "discord", qq),
               std::invalid_argument);
  EXPECT_THROW(
      (void)obcx::core::make_existing_bot_operation_endpoint("", "qq", qq),
      std::invalid_argument);
}

TEST(BotOperationDispatcherTest, ExistingWrappersSendAndDeleteTypedMessages) {
  obcx::core::QQTelegramOperationDispatcher dispatcher;
  auto qq = std::make_shared<ScriptedQQBot>();
  auto telegram = std::make_shared<ScriptedTelegramBot>();
  obcx::core::register_existing_bot_operation_endpoint(dispatcher, "qq-main",
                                                       "qq", qq);
  obcx::core::register_existing_bot_operation_endpoint(dispatcher, "tg-main",
                                                       "telegram", telegram);

  const GroupTarget qq_target{
      .installation = {.installation_id = "qq-main",
                       .surface = BotSurface::OneBot11Qq},
      .native_group_id = "qq-group",
  };
  const GroupTarget tg_target{
      .installation = {.installation_id = "tg-main",
                       .surface = BotSurface::TelegramBotApi},
      .native_group_id = "tg-chat",
  };
  const auto qq_send = run(dispatcher.execute(
      SendGroupMessageRequest{.target = qq_target, .message = text_message()}));
  ASSERT_TRUE(qq_send.ok());
  EXPECT_EQ(qq_send.value->primary().native_message_id, "7001");
  EXPECT_EQ(qq->last_group, "qq-group");

  const auto tg_send = run(dispatcher.execute(
      SendGroupMessageRequest{.target = tg_target, .message = text_message()}));
  ASSERT_TRUE(tg_send.ok());
  EXPECT_EQ(tg_send.value->primary().native_message_id, "8001");
  EXPECT_EQ(telegram->last_group, "tg-chat");

  const auto qq_delete = run(dispatcher.execute(obcx::bot::DeleteMessageRequest{
      .message = {.group = qq_target, .native_message_id = "70"}}));
  ASSERT_TRUE(qq_delete.ok());
  EXPECT_EQ(qq->last_delete, "70");

  const auto tg_delete = run(dispatcher.execute(obcx::bot::DeleteMessageRequest{
      .message = {.group = tg_target, .native_message_id = "80"}}));
  ASSERT_TRUE(tg_delete.ok());
  EXPECT_EQ(telegram->last_delete, "tg-chat:80");
}

TEST(BotOperationDispatcherTest, OneBotWrapperHandlesCurrentProviderActions) {
  obcx::core::QQTelegramOperationDispatcher dispatcher;
  auto qq = std::make_shared<ScriptedQQBot>();
  obcx::core::register_existing_bot_operation_endpoint(dispatcher, "qq-main",
                                                       "qq", qq);
  const BotInstallationRef installation{.installation_id = "qq-main",
                                        .surface = BotSurface::OneBot11Qq};
  const GroupTarget target{.installation = installation,
                           .native_group_id = "qq-group"};

  const auto member =
      run(dispatcher.execute(obcx::bot::GetOneBotGroupMemberRequest{
          .target = target, .user_id = "456", .no_cache = true}));
  ASSERT_TRUE(member.ok());
  EXPECT_EQ(member.value->user_id, "456");
  EXPECT_EQ(member.value->nickname, "nick");
  EXPECT_EQ(member.value->card, "card");
  EXPECT_EQ(member.value->title, "title");
  EXPECT_TRUE(qq->last_no_cache);

  const auto forward =
      run(dispatcher.execute(obcx::bot::GetOneBotForwardMessageRequest{
          .installation = installation, .forward_id = "forward-7"}));
  ASSERT_TRUE(forward.ok());
  EXPECT_EQ(forward.value->messages.size(), 1U);
  EXPECT_EQ(qq->last_forward, "forward-7");

  const auto group_file =
      run(dispatcher.execute(obcx::bot::ResolveOneBotGroupFileRequest{
          .target = target, .file_id = "group-file"}));
  ASSERT_TRUE(group_file.ok());
  EXPECT_EQ(group_file.value->url, "https://example.test/group");
  EXPECT_EQ(qq->last_file, "group-file");

  const auto private_file = run(dispatcher.execute(
      obcx::bot::ResolveOneBotPrivateFileRequest{.installation = installation,
                                                 .user_id = "456",
                                                 .file_id = "private-file"}));
  ASSERT_TRUE(private_file.ok());
  EXPECT_EQ(private_file.value->url, "https://example.test/private");
  EXPECT_EQ(qq->last_user, "456");
  EXPECT_EQ(qq->last_file, "private-file");

  const auto poke = run(dispatcher.execute(
      obcx::bot::PokeOneBotGroupRequest{.target = target, .user_id = "456"}));
  ASSERT_TRUE(poke.ok());
  EXPECT_EQ(poke.value->target, target);
  EXPECT_EQ(poke.value->user_id, "456");
}

TEST(BotOperationDispatcherTest,
     OneBotWrapperRejectsMalformedProviderSpecificData) {
  obcx::core::QQTelegramOperationDispatcher dispatcher;
  auto qq = std::make_shared<ScriptedQQBot>();
  qq->forward_response =
      R"({"status":"ok","retcode":0,"data":{"messages":["bad-node"]}})";
  obcx::core::register_existing_bot_operation_endpoint(dispatcher, "qq-main",
                                                       "qq", qq);
  const auto result =
      run(dispatcher.execute(obcx::bot::GetOneBotForwardMessageRequest{
          .installation = {.installation_id = "qq-main",
                           .surface = BotSurface::OneBot11Qq},
          .forward_id = "forward-7"}));
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error->code, BotOperationErrorCode::MalformedResponse);
  EXPECT_TRUE(result.error->retryable);
  EXPECT_EQ(result.error->submission_safety,
            SubmissionSafety::DefinitelyNotSubmitted);
}

TEST(BotOperationDispatcherTest, TelegramWrapperSendsTopicsAndEditsText) {
  obcx::core::QQTelegramOperationDispatcher dispatcher;
  auto telegram = std::make_shared<ScriptedTelegramBot>();
  obcx::core::register_existing_bot_operation_endpoint(dispatcher, "tg-main",
                                                       "telegram", telegram);
  const GroupTarget target{
      .installation = {.installation_id = "tg-main",
                       .surface = BotSurface::TelegramBotApi},
      .native_group_id = "tg-chat",
  };

  const auto topic =
      run(dispatcher.execute(obcx::bot::SendTelegramTopicMessageRequest{
          .target = {.group = target, .topic_id = 77},
          .message = text_message()}));
  ASSERT_TRUE(topic.ok());
  EXPECT_EQ(topic.value->primary().native_message_id, "8002");
  EXPECT_EQ(telegram->last_group, "tg-chat");
  EXPECT_EQ(telegram->last_topic, 77);

  telegram->edit_response = R"({"ok":true,"result":{"message_id":8001}})";
  const auto edited =
      run(dispatcher.execute(obcx::bot::EditTelegramMessageTextRequest{
          .message = {.group = target, .native_message_id = "8001"},
          .text = "edited",
          .parse_mode = "HTML"}));
  ASSERT_TRUE(edited.ok());
  EXPECT_EQ(telegram->last_edit_chat, "tg-chat");
  EXPECT_EQ(telegram->last_edit_message, "8001");
  EXPECT_EQ(telegram->last_edit_text, "edited");
  EXPECT_EQ(telegram->last_parse_mode, "HTML");
}

TEST(BotOperationDispatcherTest, TelegramWrapperHandlesCurrentMediaActions) {
  obcx::core::QQTelegramOperationDispatcher dispatcher;
  auto telegram = std::make_shared<ScriptedTelegramBot>();
  obcx::core::register_existing_bot_operation_endpoint(dispatcher, "tg-main",
                                                       "telegram", telegram);
  const GroupTarget target{
      .installation = {.installation_id = "tg-main",
                       .surface = BotSurface::TelegramBotApi},
      .native_group_id = "tg-chat",
  };
  const std::vector<obcx::bot::TelegramTextEntity> entities = {
      {.type = "bold", .offset = 0, .length = 4}};

  const auto photo = run(dispatcher.execute(
      obcx::bot::SendTelegramPhotoRequest{.target = target,
                                          .photo = "telegram-file-id",
                                          .caption = "photo",
                                          .caption_entities = entities}));
  ASSERT_TRUE(photo.ok());
  EXPECT_EQ(photo.value->primary().native_message_id, "8100");
  EXPECT_EQ(telegram->last_photo, "telegram-file-id");
  ASSERT_EQ(telegram->last_entities.size(), 1U);
  EXPECT_EQ(telegram->last_entities[0].type, "bold");

  const BotMessageRef reply{.group = target, .native_message_id = "80"};
  const auto media =
      run(dispatcher.execute(obcx::bot::SendTelegramMediaGroupUrlsRequest{
          .target = target,
          .media = {{.type = "photo", .source = "https://example.test/a"},
                    {.type = "photo", .source = "file-id"}},
          .caption = "album",
          .topic_id = 7,
          .reply_to = reply,
          .caption_entities = entities}));
  ASSERT_TRUE(media.ok());
  ASSERT_EQ(media.value->messages.size(), 2U);
  EXPECT_EQ(media.value->messages[1].native_message_id, "8102");
  EXPECT_EQ(telegram->last_media[0].second, "https://example.test/a");
  EXPECT_EQ(telegram->last_topic_optional, 7);
  EXPECT_EQ(telegram->last_reply, "80");

  const auto uploads =
      run(dispatcher.execute(obcx::bot::SendTelegramMediaGroupUploadsRequest{
          .target = target,
          .media = {{.type = "photo",
                     .filename = "a.jpg",
                     .mime_type = "image/jpeg",
                     .bytes = {0x00, 0x80, 0xFF}}},
          .maximum_bytes = 16}));
  ASSERT_TRUE(uploads.ok());
  ASSERT_EQ(telegram->last_uploads.size(), 1U);
  EXPECT_EQ(telegram->last_uploads[0].data.size(), 3U);
  EXPECT_EQ(static_cast<unsigned char>(telegram->last_uploads[0].data[1]),
            0x80U);
}

TEST(BotOperationDispatcherTest, TelegramWrapperFetchesBoundedFileBytes) {
  obcx::core::QQTelegramOperationDispatcher dispatcher;
  auto telegram = std::make_shared<ScriptedTelegramBot>();
  obcx::core::register_existing_bot_operation_endpoint(dispatcher, "tg-main",
                                                       "telegram", telegram);
  const obcx::bot::FetchTelegramFileRequest request{
      .installation = {.installation_id = "tg-main",
                       .surface = BotSurface::TelegramBotApi},
      .file = {.file_id = "file-id",
               .file_unique_id = "unique",
               .file_type = "photo",
               .file_size = 10,
               .mime_type = "image/jpeg",
               .file_name = "a.jpg"},
      .maximum_bytes = 16,
  };

  const auto fetched = run(dispatcher.execute(request));
  ASSERT_TRUE(fetched.ok());
  EXPECT_EQ(
      std::string(fetched.value->bytes.begin(), fetched.value->bytes.end()),
      "file-bytes");
  EXPECT_EQ(telegram->last_file.file_id, "file-id");
  EXPECT_EQ(telegram->last_download_url, *telegram->file_url);

  telegram->file_content = std::string(17, 'x');
  const auto oversized = run(dispatcher.execute(request));
  ASSERT_FALSE(oversized.ok());
  EXPECT_EQ(oversized.error->code, BotOperationErrorCode::MediaTooLarge);
  EXPECT_EQ(oversized.error->submission_safety,
            SubmissionSafety::DefinitelyNotSubmitted);
}

TEST(BotOperationDispatcherTest, TelegramWrapperRejectsMismatchedEditIdentity) {
  obcx::core::QQTelegramOperationDispatcher dispatcher;
  auto telegram = std::make_shared<ScriptedTelegramBot>();
  telegram->edit_response = R"({"ok":true,"result":{"message_id":9999}})";
  obcx::core::register_existing_bot_operation_endpoint(dispatcher, "tg-main",
                                                       "telegram", telegram);
  const auto result =
      run(dispatcher.execute(obcx::bot::EditTelegramMessageTextRequest{
          .message = {.group = {.installation =
                                    {.installation_id = "tg-main",
                                     .surface = BotSurface::TelegramBotApi},
                                .native_group_id = "tg-chat"},
                      .native_message_id = "8001"},
          .text = "edited"}));
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error->code, BotOperationErrorCode::MalformedResponse);
  EXPECT_EQ(result.error->submission_safety,
            SubmissionSafety::PossiblySubmitted);
}

TEST(BotOperationDispatcherTest,
     ExistingWrappersNeverFabricateMalformedSendIdentity) {
  obcx::core::QQTelegramOperationDispatcher dispatcher;
  auto telegram = std::make_shared<ScriptedTelegramBot>();
  telegram->send_response = R"({"ok":true,"result":{}})";
  obcx::core::register_existing_bot_operation_endpoint(dispatcher, "tg-main",
                                                       "telegram", telegram);

  const auto result = run(dispatcher.execute(SendGroupMessageRequest{
      .target = {.installation = {.installation_id = "tg-main",
                                  .surface = BotSurface::TelegramBotApi},
                 .native_group_id = "tg-chat"},
      .message = text_message(),
  }));
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error->code, BotOperationErrorCode::MalformedResponse);
  EXPECT_EQ(result.error->submission_safety,
            SubmissionSafety::PossiblySubmitted);
}

TEST(BotOperationDispatcherTest, ExistingWrappersPublishOnlyClosedActionSets) {
  obcx::core::QQTelegramOperationDispatcher dispatcher;
  auto qq = std::make_shared<ScriptedQQBot>();
  auto telegram = std::make_shared<ScriptedTelegramBot>();
  auto no_upload = std::make_shared<NoUploadTelegramBot>();
  obcx::core::register_existing_bot_operation_endpoint(dispatcher, "qq-main",
                                                       "qq", qq);
  obcx::core::register_existing_bot_operation_endpoint(dispatcher, "tg-main",
                                                       "telegram", telegram);
  obcx::core::register_existing_bot_operation_endpoint(
      dispatcher, "tg-no-upload", "telegram", no_upload);

  const auto onebot = dispatcher.supported_actions(
      {.installation_id = "qq-main", .surface = BotSurface::OneBot11Qq});
  ASSERT_TRUE(onebot.ok());
  const std::set<BotAction> expected_onebot = {
      BotAction::SendGroupMessage,       BotAction::DeleteMessage,
      BotAction::GetOneBotGroupMember,   BotAction::GetOneBotForwardMessage,
      BotAction::ResolveOneBotGroupFile, BotAction::ResolveOneBotPrivateFile,
      BotAction::PokeOneBotGroup,
  };
  EXPECT_EQ((std::set<BotAction>{onebot.value->actions.begin(),
                                 onebot.value->actions.end()}),
            expected_onebot);
  EXPECT_TRUE(onebot.value->supports(BotAction::PokeOneBotGroup));
  EXPECT_FALSE(onebot.value->supports(BotAction::SendTelegramTopicMessage));
  EXPECT_EQ(onebot.value->installation.surface, BotSurface::OneBot11Qq);

  const auto full_telegram = dispatcher.supported_actions(
      {.installation_id = "tg-main", .surface = BotSurface::TelegramBotApi});
  ASSERT_TRUE(full_telegram.ok());
  const std::set<BotAction> expected_telegram = {
      BotAction::SendGroupMessage,
      BotAction::DeleteMessage,
      BotAction::SendTelegramTopicMessage,
      BotAction::EditTelegramMessageText,
      BotAction::SendTelegramPhoto,
      BotAction::SendTelegramMediaGroupUrls,
      BotAction::SendTelegramMediaGroupUploads,
      BotAction::FetchTelegramFile,
  };
  EXPECT_EQ((std::set<BotAction>{full_telegram.value->actions.begin(),
                                 full_telegram.value->actions.end()}),
            expected_telegram);
  EXPECT_TRUE(
      full_telegram.value->supports(BotAction::SendTelegramMediaGroupUploads));
  EXPECT_FALSE(full_telegram.value->supports(BotAction::PokeOneBotGroup));

  const auto reduced_telegram =
      dispatcher.supported_actions({.installation_id = "tg-no-upload",
                                    .surface = BotSurface::TelegramBotApi});
  ASSERT_TRUE(reduced_telegram.ok());
  EXPECT_EQ(reduced_telegram.value->actions.size(), 7U);
  EXPECT_FALSE(reduced_telegram.value->supports(
      BotAction::SendTelegramMediaGroupUploads));
  for (const auto action : reduced_telegram.value->actions) {
    EXPECT_TRUE(
        obcx::bot::action_supports_surface(action, BotSurface::TelegramBotApi));
  }

  const auto unavailable_upload =
      run(dispatcher.execute(obcx::bot::SendTelegramMediaGroupUploadsRequest{
          .target = {.installation = {.installation_id = "tg-no-upload",
                                      .surface = BotSurface::TelegramBotApi},
                     .native_group_id = "chat"},
          .media = {{.type = "photo",
                     .filename = "a.jpg",
                     .mime_type = "image/jpeg",
                     .bytes = {1}}},
          .maximum_bytes = 16}));
  ASSERT_FALSE(unavailable_upload.ok());
  EXPECT_EQ(unavailable_upload.error->code,
            BotOperationErrorCode::UnsupportedAction);
}

TEST(BotOperationDispatcherTest,
     ExistingWrappersPropagateExplicitProviderRejection) {
  obcx::core::QQTelegramOperationDispatcher dispatcher;
  auto qq = std::make_shared<ScriptedQQBot>();
  auto telegram = std::make_shared<ScriptedTelegramBot>();
  qq->send_response =
      R"({"status":"failed","retcode":1403,"message":"denied"})";
  telegram->send_response =
      R"({"ok":false,"error_code":429,"description":"rate limited","parameters":{"retry_after":2}})";
  obcx::core::register_existing_bot_operation_endpoint(dispatcher, "qq-main",
                                                       "qq", qq);
  obcx::core::register_existing_bot_operation_endpoint(dispatcher, "tg-main",
                                                       "telegram", telegram);

  const auto qq_result = run(dispatcher.execute(SendGroupMessageRequest{
      .target = {.installation = {.installation_id = "qq-main",
                                  .surface = BotSurface::OneBot11Qq},
                 .native_group_id = "group"},
      .message = text_message()}));
  ASSERT_FALSE(qq_result.ok());
  EXPECT_EQ(qq_result.error->code, BotOperationErrorCode::ProviderRejected);
  EXPECT_EQ(qq_result.error->submission_safety,
            SubmissionSafety::DefinitelyNotSubmitted);

  const auto telegram_result = run(dispatcher.execute(SendGroupMessageRequest{
      .target = {.installation = {.installation_id = "tg-main",
                                  .surface = BotSurface::TelegramBotApi},
                 .native_group_id = "chat"},
      .message = text_message()}));
  ASSERT_FALSE(telegram_result.ok());
  EXPECT_EQ(telegram_result.error->code,
            BotOperationErrorCode::ProviderRejected);
  EXPECT_TRUE(telegram_result.error->retryable);
  EXPECT_EQ(telegram_result.error->retry_after,
            std::chrono::milliseconds{2000});
}

TEST(BotOperationDispatcherTest,
     RegistersExactInstallationsAndRejectsDuplicates) {
  obcx::core::QQTelegramOperationDispatcher dispatcher;
  auto telegram = std::make_shared<RecordingEndpoint>(
      BotInstallationRef{.installation_id = "tg-main",
                         .surface = BotSurface::TelegramBotApi},
      std::vector{BotAction::SendGroupMessage});
  dispatcher.register_endpoint(telegram);
  EXPECT_EQ(dispatcher.endpoint_count(), 1U);
  EXPECT_THROW(dispatcher.register_endpoint(telegram), std::invalid_argument);
  EXPECT_THROW(dispatcher.register_endpoint(nullptr), std::invalid_argument);

  auto invalid = std::make_shared<RecordingEndpoint>(
      BotInstallationRef{.installation_id = "tg-invalid",
                         .surface = BotSurface::TelegramBotApi},
      std::vector{BotAction::PokeOneBotGroup});
  EXPECT_THROW(dispatcher.register_endpoint(invalid), std::invalid_argument);
}

TEST(BotOperationDispatcherTest, ReportsOnlyTheExactEndpointActions) {
  obcx::core::QQTelegramOperationDispatcher dispatcher;
  const BotInstallationRef installation{.installation_id = "qq-main",
                                        .surface = BotSurface::OneBot11Qq};
  dispatcher.register_endpoint(std::make_shared<RecordingEndpoint>(
      installation, std::vector{BotAction::SendGroupMessage,
                                BotAction::GetOneBotGroupMember}));

  const auto supported = dispatcher.supported_actions(installation);
  ASSERT_TRUE(supported.ok());
  EXPECT_TRUE(supported.value->supports(BotAction::SendGroupMessage));
  EXPECT_TRUE(supported.value->supports(BotAction::GetOneBotGroupMember));
  EXPECT_FALSE(supported.value->supports(BotAction::DeleteMessage));
  EXPECT_EQ(
      nlohmann::json(*supported.value).get<obcx::bot::SupportedBotActions>(),
      *supported.value);
}

TEST(BotOperationDispatcherTest, DispatchesByExactInstallation) {
  obcx::core::QQTelegramOperationDispatcher dispatcher;
  auto endpoint = std::make_shared<RecordingEndpoint>(
      BotInstallationRef{.installation_id = "qq-main",
                         .surface = BotSurface::OneBot11Qq},
      std::vector{BotAction::SendGroupMessage});
  dispatcher.register_endpoint(endpoint);

  const SendGroupMessageRequest request{
      .target = {.installation = endpoint->installation(),
                 .native_group_id = "123"},
      .message = text_message(),
  };
  const auto result = run(dispatcher.execute(request));
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value->primary().native_message_id, "message-7");
  EXPECT_EQ(endpoint->send_calls.load(), 1);
}

TEST(BotOperationDispatcherTest, MissingAndWrongSurfaceRoutesDoNoIo) {
  obcx::core::QQTelegramOperationDispatcher dispatcher;
  auto endpoint = std::make_shared<RecordingEndpoint>(
      BotInstallationRef{.installation_id = "qq-main",
                         .surface = BotSurface::OneBot11Qq},
      std::vector{BotAction::SendGroupMessage});
  dispatcher.register_endpoint(endpoint);

  SendGroupMessageRequest request{
      .target = {.installation = {.installation_id = "missing",
                                  .surface = BotSurface::OneBot11Qq},
                 .native_group_id = "123"},
      .message = text_message(),
  };
  auto result = run(dispatcher.execute(request));
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error->code, BotOperationErrorCode::RouteNotFound);

  request.target.installation = {.installation_id = "qq-main",
                                 .surface = BotSurface::TelegramBotApi};
  result = run(dispatcher.execute(request));
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error->code, BotOperationErrorCode::SurfaceMismatch);
  EXPECT_EQ(endpoint->send_calls.load(), 0);
}

TEST(BotOperationDispatcherTest, InvalidAndUnsupportedRequestsDoNoIo) {
  obcx::core::QQTelegramOperationDispatcher dispatcher;
  auto endpoint = std::make_shared<RecordingEndpoint>(
      BotInstallationRef{.installation_id = "tg-main",
                         .surface = BotSurface::TelegramBotApi},
      std::vector{BotAction::SendGroupMessage});
  dispatcher.register_endpoint(endpoint);

  SendGroupMessageRequest invalid{
      .target = {.installation = endpoint->installation(),
                 .native_group_id = ""},
      .message = text_message(),
  };
  const auto invalid_result = run(dispatcher.execute(invalid));
  ASSERT_FALSE(invalid_result.ok());
  EXPECT_EQ(invalid_result.error->code, BotOperationErrorCode::InvalidRequest);

  const obcx::bot::DeleteMessageRequest unsupported{
      .message = {.group = {.installation = endpoint->installation(),
                            .native_group_id = "chat"},
                  .native_message_id = "42"},
  };
  const auto unsupported_result = run(dispatcher.execute(unsupported));
  ASSERT_FALSE(unsupported_result.ok());
  EXPECT_EQ(unsupported_result.error->code,
            BotOperationErrorCode::UnsupportedAction);
  EXPECT_EQ(endpoint->send_calls.load(), 0);
}

TEST(BotOperationDispatcherTest, SideEffectExceptionBecomesOutcomeUnknown) {
  obcx::core::QQTelegramOperationDispatcher dispatcher;
  auto endpoint = std::make_shared<RecordingEndpoint>(
      BotInstallationRef{.installation_id = "tg-main",
                         .surface = BotSurface::TelegramBotApi},
      std::vector{BotAction::SendGroupMessage});
  endpoint->send_exception =
      "https://api.telegram.org/file/bot123:secret/x?token=value";
  dispatcher.register_endpoint(endpoint);

  const auto result = run(dispatcher.execute(SendGroupMessageRequest{
      .target = {.installation = endpoint->installation(),
                 .native_group_id = "chat"},
      .message = text_message(),
  }));
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error->code, BotOperationErrorCode::OutcomeUnknown);
  EXPECT_EQ(result.error->submission_safety,
            SubmissionSafety::PossiblySubmitted);
  EXPECT_EQ(result.error->message, "[redacted provider diagnostic]");
}

TEST(BotOperationDispatcherTest,
     PreSubmissionHttpFailureIsDefinitelyNotSubmittedAndRetryable) {
  obcx::core::QQTelegramOperationDispatcher dispatcher;
  auto endpoint = std::make_shared<RecordingEndpoint>(
      BotInstallationRef{.installation_id = "tg-main",
                         .surface = BotSurface::TelegramBotApi},
      std::vector{BotAction::SendGroupMessage});
  endpoint->send_exception = "HTTP POST request failed: Connection refused";
  endpoint->send_http_error_state =
      obcx::network::HttpRequestSubmissionState::DefinitelyNotSubmitted;
  dispatcher.register_endpoint(endpoint);

  const auto result = run(dispatcher.execute(SendGroupMessageRequest{
      .target = {.installation = endpoint->installation(),
                 .native_group_id = "chat"},
      .message = text_message(),
  }));
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error->code, BotOperationErrorCode::TransportFailure);
  EXPECT_TRUE(result.error->retryable);
  EXPECT_EQ(result.error->submission_safety,
            SubmissionSafety::DefinitelyNotSubmitted);
}

TEST(BotOperationDispatcherTest,
     PostSubmissionHttpFailureRemainsOutcomeUnknown) {
  obcx::core::QQTelegramOperationDispatcher dispatcher;
  auto endpoint = std::make_shared<RecordingEndpoint>(
      BotInstallationRef{.installation_id = "tg-main",
                         .surface = BotSurface::TelegramBotApi},
      std::vector{BotAction::SendGroupMessage});
  endpoint->send_exception = "HTTP response stream truncated";
  endpoint->send_http_error_state =
      obcx::network::HttpRequestSubmissionState::PossiblySubmitted;
  dispatcher.register_endpoint(endpoint);

  const auto result = run(dispatcher.execute(SendGroupMessageRequest{
      .target = {.installation = endpoint->installation(),
                 .native_group_id = "chat"},
      .message = text_message(),
  }));
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error->code, BotOperationErrorCode::OutcomeUnknown);
  EXPECT_FALSE(result.error->retryable);
  EXPECT_EQ(result.error->submission_safety,
            SubmissionSafety::PossiblySubmitted);
}

TEST(BotOperationDispatcherTest,
     ReadOnlyExceptionIsDefinitelyNotSubmittedAndRetryable) {
  obcx::core::QQTelegramOperationDispatcher dispatcher;
  auto endpoint = std::make_shared<RecordingEndpoint>(
      BotInstallationRef{.installation_id = "qq-main",
                         .surface = BotSurface::OneBot11Qq},
      std::vector{BotAction::GetOneBotGroupMember});
  endpoint->member_exception = "temporary lookup failure";
  dispatcher.register_endpoint(endpoint);

  const auto result = run(dispatcher.execute(GetOneBotGroupMemberRequest{
      .target = {.installation = endpoint->installation(),
                 .native_group_id = "123"},
      .user_id = "456",
  }));
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error->code, BotOperationErrorCode::TransportFailure);
  EXPECT_TRUE(result.error->retryable);
  EXPECT_EQ(result.error->submission_safety,
            SubmissionSafety::DefinitelyNotSubmitted);
}

} // namespace
