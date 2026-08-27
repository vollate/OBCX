#include "core/onebot11_bot_operations.hpp"
#include "core/telegram_bot_operations.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <set>
#include <stdexcept>
#include <string>

namespace {

using obcx::bot::action_id;
using obcx::bot::BotAction;
using obcx::bot::BotInstallationRef;
using obcx::bot::BotMessageRef;
using obcx::bot::BotOperationError;
using obcx::bot::BotOperationErrorCode;
using obcx::bot::BotOperationResult;
using obcx::bot::BotSurface;
using obcx::bot::GroupTarget;
using obcx::bot::SubmissionSafety;
using obcx::bot::TelegramTopicTarget;
using obcx::bot::action_ids::all;

template <typename T> void expect_stable_round_trip(const T &value) {
  const auto first = nlohmann::json(value);
  const auto decoded = first.get<T>();
  const auto second = nlohmann::json(decoded);
  EXPECT_EQ(second.dump(), first.dump());
}

TEST(BotOperationTypesTest, ClosedActionMatrixContainsThirteenUniqueIds) {
  const std::set<std::string_view> unique(all.begin(), all.end());
  EXPECT_EQ(all.size(), 13U);
  EXPECT_EQ(unique.size(), all.size());
  for (std::size_t index = 0; index < all.size(); ++index) {
    const auto action = static_cast<BotAction>(index);
    EXPECT_EQ(action_id(action), all[index]);
    EXPECT_EQ(obcx::bot::bot_action_from_id(all[index]), action);
  }
  EXPECT_THROW((void)obcx::bot::bot_action_from_id("message.history"),
               std::invalid_argument);
}

TEST(BotOperationTypesTest, ScopedReferencesRoundTripDeterministically) {
  const BotMessageRef message{
      .group = {.installation = {.installation_id = "telegram-main",
                                 .surface = BotSurface::TelegramBotApi},
                .native_group_id = "-1001"},
      .native_message_id = "42",
  };
  const nlohmann::json document = message;
  EXPECT_EQ(
      document.dump(),
      R"({"group":{"installation":{"installation_id":"telegram-main","surface":"telegram.bot_api"},"native_group_id":"-1001"},"native_message_id":"42"})");
  EXPECT_EQ(document.get<BotMessageRef>(), message);

  auto other = message;
  other.group.installation.installation_id = "telegram-secondary";
  EXPECT_NE(other, message);
}

TEST(BotOperationTypesTest, TelegramTopicRequiresTelegramAndPositiveId) {
  TelegramTopicTarget valid{
      .group = {.installation = {.installation_id = "tg",
                                 .surface = BotSurface::TelegramBotApi},
                .native_group_id = "chat"},
      .topic_id = 7,
  };
  EXPECT_NO_THROW(valid.validate());
  EXPECT_EQ(nlohmann::json(valid).get<TelegramTopicTarget>(), valid);

  valid.topic_id = 0;
  EXPECT_THROW(valid.validate(), std::invalid_argument);
  valid.topic_id = 7;
  valid.group.installation.surface = BotSurface::OneBot11Qq;
  EXPECT_THROW(valid.validate(), std::invalid_argument);
}

TEST(BotOperationTypesTest, ResultRoundTripsSuccessAndFailure) {
  const BotMessageRef message{
      .group = {.installation = {.installation_id = "qq-main",
                                 .surface = BotSurface::OneBot11Qq},
                .native_group_id = "123"},
      .native_message_id = "456",
  };
  const auto success = BotOperationResult<BotMessageRef>::success(message);
  const auto success_document = nlohmann::json(success);
  EXPECT_TRUE(success_document.at("ok"));
  EXPECT_EQ(success_document.get<BotOperationResult<BotMessageRef>>(), success);

  const BotOperationError error{
      .code = BotOperationErrorCode::ProviderRejected,
      .message = "provider rejected request",
      .provider_code = "429",
      .retry_after = std::chrono::milliseconds{1500},
      .retryable = true,
      .submission_safety = SubmissionSafety::DefinitelyNotSubmitted,
  };
  const auto failure = BotOperationResult<BotMessageRef>::failure(error);
  const auto failure_document = nlohmann::json(failure);
  EXPECT_FALSE(failure_document.at("ok"));
  EXPECT_EQ(failure_document.get<BotOperationResult<BotMessageRef>>(), failure);
}

TEST(BotOperationTypesTest, InvalidJsonAndOutcomeSafetyAreRejected) {
  EXPECT_THROW((void)nlohmann::json({{"installation_id", "telegram-main"},
                                     {"surface", "qq.official"}})
                   .get<BotInstallationRef>(),
               std::invalid_argument);
  EXPECT_THROW((void)nlohmann::json({{"surface", "telegram.bot_api"}})
                   .get<BotInstallationRef>(),
               std::invalid_argument);
  EXPECT_THROW((void)nlohmann::json(
                   {{"installation_id", ""}, {"surface", "telegram.bot_api"}})
                   .get<BotInstallationRef>(),
               std::invalid_argument);

  BotOperationError unknown{
      .code = BotOperationErrorCode::OutcomeUnknown,
      .message = "unknown",
      .submission_safety = SubmissionSafety::DefinitelyNotSubmitted,
  };
  EXPECT_THROW(unknown.validate(), std::invalid_argument);
  unknown.submission_safety = SubmissionSafety::PossiblySubmitted;
  EXPECT_NO_THROW(unknown.validate());
}

TEST(BotOperationTypesTest, CommonAndTelegramMessageRequestsRoundTrip) {
  const GroupTarget qq_target{
      .installation = {.installation_id = "qq-main",
                       .surface = BotSurface::OneBot11Qq},
      .native_group_id = "123",
  };
  const obcx::common::Message payload = {
      {.type = "reply", .data = {{"id", "41"}}},
      {.type = "text", .data = {{"text", "hello"}}},
  };
  const obcx::bot::SendGroupMessageRequest send{.target = qq_target,
                                                .message = payload};
  const auto send_document = nlohmann::json(send);
  EXPECT_EQ(send_document.at("action"), "message.send_group");
  const auto decoded_send =
      send_document.get<obcx::bot::SendGroupMessageRequest>();
  EXPECT_EQ(nlohmann::json(decoded_send), send_document);

  const obcx::bot::SendTelegramTopicMessageRequest topic{
      .target = {.group = {.installation = {.installation_id = "tg-main",
                                            .surface =
                                                BotSurface::TelegramBotApi},
                           .native_group_id = "-1001"},
                 .topic_id = 7},
      .message = payload,
  };
  const auto topic_document = nlohmann::json(topic);
  EXPECT_EQ(topic_document.at("action"), "telegram.message.send_topic");
  EXPECT_EQ(
      nlohmann::json(
          topic_document.get<obcx::bot::SendTelegramTopicMessageRequest>()),
      topic_document);
}

TEST(BotOperationTypesTest, DeleteAndEditRequestsKeepMessageScope) {
  const BotMessageRef telegram_message{
      .group = {.installation = {.installation_id = "tg-main",
                                 .surface = BotSurface::TelegramBotApi},
                .native_group_id = "-1001"},
      .native_message_id = "42",
  };
  const obcx::bot::DeleteMessageRequest deletion{.message = telegram_message};
  const auto delete_document = nlohmann::json(deletion);
  EXPECT_EQ(delete_document.at("message").at("group").at("native_group_id"),
            "-1001");
  EXPECT_EQ(delete_document.at("message").at("native_message_id"), "42");
  EXPECT_EQ(delete_document.get<obcx::bot::DeleteMessageRequest>().message,
            telegram_message);

  const obcx::bot::EditTelegramMessageTextRequest edit{
      .message = telegram_message,
      .text = "edited",
      .parse_mode = "HTML",
  };
  const auto edit_document = nlohmann::json(edit);
  EXPECT_EQ(nlohmann::json(
                edit_document.get<obcx::bot::EditTelegramMessageTextRequest>()),
            edit_document);

  auto wrong_surface = edit;
  wrong_surface.message.group.installation.surface = BotSurface::OneBot11Qq;
  EXPECT_THROW(wrong_surface.validate(), std::invalid_argument);
}

TEST(BotOperationTypesTest, SendAndMutationResultsRoundTrip) {
  const BotMessageRef message{
      .group = {.installation = {.installation_id = "tg-main",
                                 .surface = BotSurface::TelegramBotApi},
                .native_group_id = "-1001"},
      .native_message_id = "42",
  };
  const obcx::bot::SendMessageResult sent{.messages = {message}};
  EXPECT_EQ(nlohmann::json(sent).get<obcx::bot::SendMessageResult>(), sent);
  EXPECT_EQ(sent.primary(), message);

  const obcx::bot::DeleteMessageResult deleted{.message = message};
  EXPECT_EQ(nlohmann::json(deleted).get<obcx::bot::DeleteMessageResult>(),
            deleted);
  const obcx::bot::EditMessageTextResult edited{.message = message};
  EXPECT_EQ(nlohmann::json(edited).get<obcx::bot::EditMessageTextResult>(),
            edited);
}

TEST(BotOperationTypesTest, MessageRequestValidationRejectsInvalidPayloads) {
  const auto target = GroupTarget{
      .installation = {.installation_id = "qq-main",
                       .surface = BotSurface::OneBot11Qq},
      .native_group_id = "123",
  };
  EXPECT_THROW(
      (obcx::bot::SendGroupMessageRequest{.target = target, .message = {}})
          .validate(),
      std::invalid_argument);

  auto mismatched = nlohmann::json{
      {"action", "telegram.message.send_topic"},
      {"target", target},
      {"message",
       nlohmann::json::array({{{"type", "text"}, {"data", {{"text", "x"}}}}})},
  };
  EXPECT_THROW((void)mismatched.get<obcx::bot::SendGroupMessageRequest>(),
               std::invalid_argument);
}

TEST(BotOperationTypesTest, TelegramPhotoAndUrlMediaRequestsRoundTrip) {
  const GroupTarget target{
      .installation = {.installation_id = "tg-main",
                       .surface = BotSurface::TelegramBotApi},
      .native_group_id = "-1001",
  };
  const std::vector<obcx::bot::TelegramTextEntity> entities = {
      {.type = "bold", .offset = 0, .length = 4},
  };
  const obcx::bot::SendTelegramPhotoRequest photo{
      .target = target,
      .photo = "telegram-file-id",
      .caption = "test",
      .caption_entities = entities,
  };
  const auto photo_document = nlohmann::json(photo);
  EXPECT_EQ(
      nlohmann::json(photo_document.get<obcx::bot::SendTelegramPhotoRequest>()),
      photo_document);

  const BotMessageRef reply{.group = target, .native_message_id = "40"};
  const obcx::bot::SendTelegramMediaGroupUrlsRequest media{
      .target = target,
      .media = {{.type = "photo", .source = "https://example.test/a.jpg"},
                {.type = "photo", .source = "telegram-file-id"}},
      .caption = "album",
      .topic_id = 7,
      .reply_to = reply,
      .caption_entities = entities,
  };
  const auto media_document = nlohmann::json(media);
  EXPECT_EQ(
      nlohmann::json(
          media_document.get<obcx::bot::SendTelegramMediaGroupUrlsRequest>()),
      media_document);
}

TEST(BotOperationTypesTest, TelegramUploadBytesAreBoundedAndRoundTrip) {
  const GroupTarget target{
      .installation = {.installation_id = "tg-main",
                       .surface = BotSurface::TelegramBotApi},
      .native_group_id = "-1001",
  };
  obcx::bot::SendTelegramMediaGroupUploadsRequest request{
      .target = target,
      .media = {{.type = "photo",
                 .filename = "image.jpg",
                 .mime_type = "image/jpeg",
                 .bytes = {0x00, 0x7F, 0x80, 0xFF}},
                {.type = "photo",
                 .filename = "image-2.jpg",
                 .mime_type = "image/jpeg",
                 .bytes = {0x01, 0x02}}},
      .caption = "upload",
      .maximum_bytes = 16,
  };
  const auto document = nlohmann::json(request);
  EXPECT_EQ(
      document.get<obcx::bot::SendTelegramMediaGroupUploadsRequest>().media,
      request.media);
  EXPECT_NO_THROW(request.validate());

  request.maximum_bytes = 3;
  EXPECT_THROW(request.validate(), std::invalid_argument);
  request.maximum_bytes = obcx::bot::maximum_actor_media_bytes + 1;
  EXPECT_THROW(request.validate(), std::invalid_argument);
  request.maximum_bytes = 16;
  request.media.resize(1);
  EXPECT_THROW(request.validate(), std::invalid_argument);
}

TEST(BotOperationTypesTest, TelegramFileFetchKeepsMetadataAndByteLimit) {
  const BotInstallationRef installation{
      .installation_id = "tg-main",
      .surface = BotSurface::TelegramBotApi,
  };
  obcx::bot::FetchTelegramFileRequest request{
      .installation = installation,
      .file = {.file_id = "file-id",
               .file_unique_id = "unique-id",
               .file_type = "sticker",
               .file_size = 4,
               .mime_type = "image/webp",
               .file_name = "sticker.webp"},
      .maximum_bytes = 16,
  };
  const auto request_document = nlohmann::json(request);
  EXPECT_EQ(request_document.get<obcx::bot::FetchTelegramFileRequest>().file,
            request.file);

  const obcx::bot::FetchedTelegramFile fetched{
      .installation = installation,
      .file = request.file,
      .bytes = {0x52, 0x49, 0x46, 0x46},
  };
  EXPECT_EQ(nlohmann::json(fetched).get<obcx::bot::FetchedTelegramFile>(),
            fetched);

  request.file.file_size = 17;
  EXPECT_THROW(request.validate(), std::invalid_argument);
  request.installation.surface = BotSurface::OneBot11Qq;
  EXPECT_THROW(request.validate(), std::invalid_argument);
}

TEST(BotOperationTypesTest, TelegramMediaRejectsWrongTargetAndGroupShape) {
  obcx::bot::SendTelegramMediaGroupUrlsRequest request{
      .target = {.installation = {.installation_id = "qq-main",
                                  .surface = BotSurface::OneBot11Qq},
                 .native_group_id = "123"},
      .media = {{.type = "photo", .source = "https://example.test/a.jpg"}},
  };
  EXPECT_THROW(request.validate(), std::invalid_argument);

  request.target.installation = {.installation_id = "tg-main",
                                 .surface = BotSurface::TelegramBotApi};
  EXPECT_THROW(request.validate(), std::invalid_argument);
  request.media.clear();
  EXPECT_THROW(request.validate(), std::invalid_argument);
  request.media.resize(11, {.type = "photo", .source = "file-id"});
  EXPECT_THROW(request.validate(), std::invalid_argument);
}

TEST(BotOperationTypesTest, OneBotMemberAndForwardValuesRoundTrip) {
  const GroupTarget target{
      .installation = {.installation_id = "qq-main",
                       .surface = BotSurface::OneBot11Qq},
      .native_group_id = "123",
  };
  const obcx::bot::GetOneBotGroupMemberRequest request{
      .target = target,
      .user_id = "456",
      .no_cache = false,
  };
  const auto request_document = nlohmann::json(request);
  EXPECT_EQ(nlohmann::json(
                request_document.get<obcx::bot::GetOneBotGroupMemberRequest>()),
            request_document);

  const obcx::bot::OneBotGroupMember member{
      .target = target,
      .user_id = "456",
      .nickname = "nickname",
      .card = "group card",
      .title = "title",
  };
  EXPECT_EQ(nlohmann::json(member).get<obcx::bot::OneBotGroupMember>(), member);

  const obcx::bot::OneBotForwardMessage forward{
      .installation = target.installation,
      .forward_id = "forward-7",
      .messages = nlohmann::json::array(
          {{{"sender", {{"nickname", "alice"}}},
            {"content",
             nlohmann::json::array(
                 {{{"type", "text"}, {"data", {{"text", "hello"}}}}})}}}),
  };
  EXPECT_EQ(nlohmann::json(forward).get<obcx::bot::OneBotForwardMessage>(),
            forward);
}

TEST(BotOperationTypesTest, OneBotFileAndPokeValuesRoundTrip) {
  const GroupTarget target{
      .installation = {.installation_id = "qq-main",
                       .surface = BotSurface::OneBot11Qq},
      .native_group_id = "123",
  };
  const obcx::bot::ResolveOneBotGroupFileRequest group_request{
      .target = target,
      .file_id = "group-file",
  };
  EXPECT_EQ(nlohmann::json(group_request)
                .get<obcx::bot::ResolveOneBotGroupFileRequest>()
                .file_id,
            "group-file");
  const obcx::bot::ResolvedOneBotGroupFile group_file{
      .target = target,
      .file_id = "group-file",
      .url = "https://example.test/group-file",
  };
  EXPECT_EQ(
      nlohmann::json(group_file).get<obcx::bot::ResolvedOneBotGroupFile>(),
      group_file);

  const obcx::bot::ResolveOneBotPrivateFileRequest private_request{
      .installation = target.installation,
      .user_id = "456",
      .file_id = "private-file",
  };
  EXPECT_EQ(nlohmann::json(private_request)
                .get<obcx::bot::ResolveOneBotPrivateFileRequest>()
                .user_id,
            "456");
  const obcx::bot::ResolvedOneBotPrivateFile private_file{
      .installation = target.installation,
      .user_id = "456",
      .file_id = "private-file",
      .url = "https://example.test/private-file",
  };
  EXPECT_EQ(
      nlohmann::json(private_file).get<obcx::bot::ResolvedOneBotPrivateFile>(),
      private_file);

  const obcx::bot::PokeOneBotGroupRequest poke{
      .target = target,
      .user_id = "456",
  };
  EXPECT_EQ(
      nlohmann::json(poke).get<obcx::bot::PokeOneBotGroupRequest>().target,
      target);
  const obcx::bot::OneBotGroupPokeResult result{
      .target = target,
      .user_id = "456",
  };
  EXPECT_EQ(nlohmann::json(result).get<obcx::bot::OneBotGroupPokeResult>(),
            result);
}

TEST(BotOperationTypesTest, OneBotValuesRejectTelegramAndMalformedNodes) {
  obcx::bot::GetOneBotForwardMessageRequest request{
      .installation = {.installation_id = "tg-main",
                       .surface = BotSurface::TelegramBotApi},
      .forward_id = "forward-7",
  };
  EXPECT_THROW(request.validate(), std::invalid_argument);

  obcx::bot::OneBotForwardMessage response{
      .installation = {.installation_id = "qq-main",
                       .surface = BotSurface::OneBot11Qq},
      .forward_id = "forward-7",
      .messages = nlohmann::json::array({"not-an-object"}),
  };
  EXPECT_THROW(response.validate(), std::invalid_argument);
}

TEST(BotOperationTypesTest, EveryClosedActionRequestHasStableJson) {
  const BotInstallationRef telegram{.installation_id = "tg-main",
                                    .surface = BotSurface::TelegramBotApi};
  const BotInstallationRef onebot{.installation_id = "qq-main",
                                  .surface = BotSurface::OneBot11Qq};
  const GroupTarget telegram_group{.installation = telegram,
                                   .native_group_id = "-1001"};
  const GroupTarget onebot_group{.installation = onebot,
                                 .native_group_id = "123"};
  const BotMessageRef telegram_message{.group = telegram_group,
                                       .native_message_id = "42"};
  const BotMessageRef onebot_message{.group = onebot_group,
                                     .native_message_id = "43"};
  const obcx::common::Message text = {
      {.type = "text", .data = {{"text", "hello"}}}};

  expect_stable_round_trip(obcx::bot::SendGroupMessageRequest{
      .target = onebot_group, .message = text});
  expect_stable_round_trip(
      obcx::bot::DeleteMessageRequest{.message = onebot_message});
  expect_stable_round_trip(obcx::bot::SendTelegramTopicMessageRequest{
      .target = {.group = telegram_group, .topic_id = 7}, .message = text});
  expect_stable_round_trip(obcx::bot::EditTelegramMessageTextRequest{
      .message = telegram_message, .text = "edited", .parse_mode = "HTML"});
  expect_stable_round_trip(obcx::bot::SendTelegramPhotoRequest{
      .target = telegram_group, .photo = "file-id"});
  expect_stable_round_trip(obcx::bot::SendTelegramMediaGroupUrlsRequest{
      .target = telegram_group,
      .media = {{.type = "photo", .source = "https://example.test/a"},
                {.type = "photo", .source = "https://example.test/b"}}});
  expect_stable_round_trip(obcx::bot::SendTelegramMediaGroupUploadsRequest{
      .target = telegram_group,
      .media = {{.type = "photo",
                 .filename = "a.jpg",
                 .mime_type = "image/jpeg",
                 .bytes = {1, 2, 3}},
                {.type = "photo",
                 .filename = "b.jpg",
                 .mime_type = "image/jpeg",
                 .bytes = {4, 5, 6}}},
      .maximum_bytes = 16});
  expect_stable_round_trip(obcx::bot::FetchTelegramFileRequest{
      .installation = telegram,
      .file = {.file_id = "file-id", .file_type = "photo"},
      .maximum_bytes = 16});
  expect_stable_round_trip(obcx::bot::GetOneBotGroupMemberRequest{
      .target = onebot_group, .user_id = "456"});
  expect_stable_round_trip(obcx::bot::GetOneBotForwardMessageRequest{
      .installation = onebot, .forward_id = "forward-id"});
  expect_stable_round_trip(obcx::bot::ResolveOneBotGroupFileRequest{
      .target = onebot_group, .file_id = "file-id"});
  expect_stable_round_trip(obcx::bot::ResolveOneBotPrivateFileRequest{
      .installation = onebot, .user_id = "456", .file_id = "file-id"});
  expect_stable_round_trip(obcx::bot::PokeOneBotGroupRequest{
      .target = onebot_group, .user_id = "456"});
}

TEST(BotOperationTypesTest, CredentialBearingErrorsMustBeRedacted) {
  const std::string unsafe =
      "GET https://api.telegram.org/file/bot123:secret/file.jpg?token=value";
  EXPECT_EQ(obcx::bot::redact_bot_diagnostic(unsafe),
            "[redacted provider diagnostic]");

  BotOperationError error{
      .code = BotOperationErrorCode::TransportFailure,
      .message = unsafe,
      .retryable = true,
      .submission_safety = SubmissionSafety::PossiblySubmitted,
  };
  EXPECT_THROW(error.validate(), std::invalid_argument);
  error.message = obcx::bot::redact_bot_diagnostic(error.message);
  EXPECT_NO_THROW(error.validate());
  const auto document = nlohmann::json(error).dump();
  EXPECT_EQ(document.find("123:secret"), std::string::npos);
  EXPECT_EQ(document.find("token=value"), std::string::npos);
}

TEST(BotOperationTypesTest, MissingRoutesAndInvalidJsonShapesAreRejected) {
  EXPECT_THROW((void)nlohmann::json({{"action", "message.send_group"},
                                     {"message", nlohmann::json::array()}})
                   .get<obcx::bot::SendGroupMessageRequest>(),
               std::invalid_argument);
  EXPECT_THROW(
      (void)nlohmann::json({{"installation_id", "tg-main"}, {"surface", 1}})
          .get<BotInstallationRef>(),
      std::invalid_argument);
  EXPECT_THROW(
      (void)nlohmann::json(
          {{"action", "telegram.media.fetch_file"},
           {"installation",
            {{"installation_id", "tg-main"}, {"surface", "telegram.bot_api"}}},
           {"file", {{"file_id", "id"}, {"file_type", "photo"}}},
           {"maximum_bytes", 0}})
          .get<obcx::bot::FetchTelegramFileRequest>(),
      std::invalid_argument);
  EXPECT_THROW(
      (void)nlohmann::json({{"ok", true}, {"error", {{"message", "invalid"}}}})
          .get<BotOperationResult<BotMessageRef>>(),
      std::invalid_argument);
}

TEST(BotOperationTypesTest, SurfaceActionCompatibilityIsClosed) {
  EXPECT_TRUE(obcx::bot::action_supports_surface(BotAction::SendGroupMessage,
                                                 BotSurface::TelegramBotApi));
  EXPECT_TRUE(obcx::bot::action_supports_surface(BotAction::SendGroupMessage,
                                                 BotSurface::OneBot11Qq));
  EXPECT_TRUE(obcx::bot::action_supports_surface(BotAction::FetchTelegramFile,
                                                 BotSurface::TelegramBotApi));
  EXPECT_FALSE(obcx::bot::action_supports_surface(BotAction::FetchTelegramFile,
                                                  BotSurface::OneBot11Qq));
  EXPECT_TRUE(obcx::bot::action_supports_surface(BotAction::PokeOneBotGroup,
                                                 BotSurface::OneBot11Qq));
  EXPECT_FALSE(obcx::bot::action_supports_surface(BotAction::PokeOneBotGroup,
                                                  BotSurface::TelegramBotApi));
}

} // namespace
