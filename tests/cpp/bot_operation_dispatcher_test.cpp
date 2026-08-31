#include "core/bot/bot_operation_dispatcher.hpp"
#include "network/http_client.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace asio = boost::asio;
using obcx::bot::BotAction;
using obcx::bot::BotInstallationRef;
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
    ++send_calls;
    if (send_http_error_state) {
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
    ++member_calls;
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

template <typename T> auto run(asio::awaitable<T> operation) -> T {
  asio::io_context context;
  auto future = asio::co_spawn(context, std::move(operation), asio::use_future);
  context.run();
  return future.get();
}

auto text_message() -> obcx::common::Message {
  return {{.type = "text", .data = {{"text", "hello"}}}};
}

TEST(BotOperationDispatcherTest,
     RegistersExactInstallationsAndRejectsDuplicatesAndInvalidSurfaces) {
  obcx::core::BotOperationDispatcher dispatcher;
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

TEST(BotOperationDispatcherTest, ClearEndpointsReleasesRegisteredOwnership) {
  obcx::core::BotOperationDispatcher dispatcher;
  auto endpoint = std::make_shared<RecordingEndpoint>(
      BotInstallationRef{.installation_id = "telegram-main",
                         .surface = BotSurface::TelegramBotApi},
      std::vector{BotAction::SendGroupMessage});
  const std::weak_ptr<RecordingEndpoint> registered = endpoint;

  dispatcher.register_endpoint(endpoint);
  endpoint.reset();
  ASSERT_FALSE(registered.expired());

  dispatcher.clear_endpoints();
  EXPECT_TRUE(registered.expired());
  EXPECT_EQ(dispatcher.endpoint_count(), 0U);
}

TEST(BotOperationDispatcherTest, ReportsOnlyExactEndpointActions) {
  obcx::core::BotOperationDispatcher dispatcher;
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
}

TEST(BotOperationDispatcherTest, DispatchesByExactInstallation) {
  obcx::core::BotOperationDispatcher dispatcher;
  auto endpoint = std::make_shared<RecordingEndpoint>(
      BotInstallationRef{.installation_id = "qq-main",
                         .surface = BotSurface::OneBot11Qq},
      std::vector{BotAction::SendGroupMessage});
  dispatcher.register_endpoint(endpoint);
  const auto result = run(dispatcher.execute(SendGroupMessageRequest{
      .target = {.installation = endpoint->installation(),
                 .native_group_id = "123"},
      .message = text_message()}));
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value->primary().native_message_id, "message-7");
  EXPECT_EQ(endpoint->send_calls.load(), 1);
}

TEST(BotOperationDispatcherTest, MissingWrongSurfaceAndUnsupportedDoNoIo) {
  obcx::core::BotOperationDispatcher dispatcher;
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

  const auto unsupported =
      run(dispatcher.execute(obcx::bot::GetOneBotGroupMemberRequest{
          .target = {.installation = endpoint->installation(),
                     .native_group_id = "123"},
          .user_id = "456"}));
  ASSERT_FALSE(unsupported.ok());
  EXPECT_EQ(unsupported.error->code, BotOperationErrorCode::UnsupportedAction);
  EXPECT_EQ(endpoint->send_calls.load(), 0);
  EXPECT_EQ(endpoint->member_calls.load(), 0);
}

TEST(BotOperationDispatcherTest, SideEffectExceptionIsConservativeAndRedacted) {
  obcx::core::BotOperationDispatcher dispatcher;
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
      .message = text_message()}));
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error->code, BotOperationErrorCode::OutcomeUnknown);
  EXPECT_EQ(result.error->submission_safety,
            SubmissionSafety::PossiblySubmitted);
  EXPECT_EQ(result.error->message, "[redacted provider diagnostic]");
}

TEST(BotOperationDispatcherTest, TransportSubmissionStateIsPreserved) {
  obcx::core::BotOperationDispatcher dispatcher;
  auto endpoint = std::make_shared<RecordingEndpoint>(
      BotInstallationRef{.installation_id = "tg-main",
                         .surface = BotSurface::TelegramBotApi},
      std::vector{BotAction::SendGroupMessage});
  dispatcher.register_endpoint(endpoint);
  const auto request = SendGroupMessageRequest{
      .target = {.installation = endpoint->installation(),
                 .native_group_id = "chat"},
      .message = text_message()};

  endpoint->send_exception = "connection refused";
  endpoint->send_http_error_state =
      obcx::network::HttpRequestSubmissionState::DefinitelyNotSubmitted;
  auto result = run(dispatcher.execute(request));
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error->code, BotOperationErrorCode::TransportFailure);
  EXPECT_TRUE(result.error->retryable);
  EXPECT_EQ(result.error->submission_safety,
            SubmissionSafety::DefinitelyNotSubmitted);

  endpoint->send_http_error_state =
      obcx::network::HttpRequestSubmissionState::PossiblySubmitted;
  result = run(dispatcher.execute(request));
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error->code, BotOperationErrorCode::OutcomeUnknown);
  EXPECT_FALSE(result.error->retryable);
  EXPECT_EQ(result.error->submission_safety,
            SubmissionSafety::PossiblySubmitted);
}

TEST(BotOperationDispatcherTest, ReadOnlyExceptionIsSafeToRetry) {
  obcx::core::BotOperationDispatcher dispatcher;
  auto endpoint = std::make_shared<RecordingEndpoint>(
      BotInstallationRef{.installation_id = "qq-main",
                         .surface = BotSurface::OneBot11Qq},
      std::vector{BotAction::GetOneBotGroupMember});
  endpoint->member_exception = "temporary lookup failure";
  dispatcher.register_endpoint(endpoint);
  const auto result = run(dispatcher.execute(GetOneBotGroupMemberRequest{
      .target = {.installation = endpoint->installation(),
                 .native_group_id = "123"},
      .user_id = "456"}));
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error->code, BotOperationErrorCode::TransportFailure);
  EXPECT_TRUE(result.error->retryable);
  EXPECT_EQ(result.error->submission_safety,
            SubmissionSafety::DefinitelyNotSubmitted);
}

} // namespace
