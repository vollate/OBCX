// Deliberately no legacy umbrella, platform DTO or runtime dependency.
#include "core/bot/messaging_client.hpp"
#include "support/sdk_gateway_fixture.hpp"

#include <gtest/gtest.h>

#include <type_traits>

namespace {
using namespace obcx::bot;

static_assert(!std::is_default_constructible_v<BotInstallationRef>);
static_assert(!std::is_default_constructible_v<GroupTarget>);
static_assert(!std::is_default_constructible_v<BotMessageRef>);
static_assert(!std::is_default_constructible_v<SendGroupMessageRequest>);

TEST(BotCommonContractTest, ReferencesHaveExplicitOpenSurfaceIdentity) {
  const BotMessageRef message{
      .group = {.installation = {.installation_id = "fixture",
                                 .surface = SurfaceId{"test.echo"}},
                .native_group_id = "group-1"},
      .native_message_id = "42"};
  EXPECT_EQ(Json(message).get<BotMessageRef>(), message);
  auto different = message;
  different.group.installation.installation_id = "second";
  EXPECT_NE(different, message);
  different = message;
  different.group.native_group_id = "group-2";
  EXPECT_NE(different, message);
  auto malformed = Json(message.group.installation);
  malformed.erase("surface");
  EXPECT_THROW((void)malformed.get<BotInstallationRef>(),
               std::invalid_argument);
}

TEST(BotCommonContractTest, CommonActionsNeedNoPlatformEnumOrCodec) {
  const SendGroupMessageRequest request{
      .target = {.installation = {.installation_id = "fixture",
                                  .surface = SurfaceId{"test.echo"}},
                 .native_group_id = "group-1"},
      .message = {{.type = "text", .data = {{"text", "hello"}}}}};
  const auto document = Json(request);
  EXPECT_EQ(document.at("action"), "message.send_group");
  EXPECT_EQ(Json(document.get<SendGroupMessageRequest>()), document);
  auto forged = document;
  forged["action"] = "test.echo.unregistered";
  EXPECT_THROW((void)forged.get<SendGroupMessageRequest>(),
               std::invalid_argument);
  const DeleteMessageRequest removal{
      .message = {.group = request.target, .native_message_id = "42"}};
  EXPECT_EQ(Json(Json(removal).get<DeleteMessageRequest>()), Json(removal));
  const auto result = BotOperationResult<DeleteMessageResult>::success(
      {.message = removal.message});
  EXPECT_EQ(Json(result).get<BotOperationResult<DeleteMessageResult>>(),
            result);
}

TEST(BotCommonContractTest, MessagingFacadeNeedsOnlyCommonSdk) {
  const BotMessageRef message{
      .group = {.installation = {.installation_id = "fixture",
                                 .surface = SurfaceId{"test.echo"}},
                .native_group_id = "group-1"},
      .native_message_id = "42"};
  obcx::tests::ReplyGateway send_gateway{
      SendGroupMessageRequest::action,
      OperationReply::success(Json(SendMessageResult{.messages = {message}}))};
  MessagingClient sender{send_gateway};
  const auto sent =
      obcx::tests::await_sdk(sender.execute(SendGroupMessageRequest{
          .target = message.group,
          .message = {{.type = "text", .data = {{"text", "hello"}}}}}));
  ASSERT_TRUE(sent.ok());
  EXPECT_EQ(sent.value->primary(), message);
  obcx::tests::ReplyGateway delete_gateway{
      DeleteMessageRequest::action,
      OperationReply::success(Json(DeleteMessageResult{.message = message}))};
  MessagingClient remover{delete_gateway};
  const auto removed = obcx::tests::await_sdk(
      remover.execute(DeleteMessageRequest{.message = message}));
  ASSERT_TRUE(removed.ok());
  EXPECT_EQ(removed.value->message, message);
}

TEST(BotCommonContractTest, ErrorsPreserveSubmissionSafetyWithoutPlatformDtos) {
  const auto failure = BotOperationResult<SendMessageResult>::failure(
      {.code = BotOperationErrorCode::OutcomeUnknown,
       .message = "result unavailable",
       .retryable = false,
       .submission_safety = SubmissionSafety::PossiblySubmitted});
  EXPECT_EQ(Json(failure).get<BotOperationResult<SendMessageResult>>(),
            failure);
  EXPECT_EQ(Json(failure).at("error").at("submission_safety"),
            "possibly_submitted");
}

} // namespace
