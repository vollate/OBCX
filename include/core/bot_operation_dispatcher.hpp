#ifndef OBCX_INCLUDE_CORE_BOT_OPERATION_DISPATCHER_HPP_
#define OBCX_INCLUDE_CORE_BOT_OPERATION_DISPATCHER_HPP_

#include "core/bot_operation_client.hpp"

#include <boost/asio/awaitable.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace obcx::core {

class BotOperationEndpoint : public bot::BotOperationClient {
public:
  [[nodiscard]] virtual auto installation() const
      -> bot::BotInstallationRef = 0;
  [[nodiscard]] virtual auto declared_actions() const
      -> std::vector<bot::BotAction> = 0;

  [[nodiscard]] auto supported_actions(const bot::BotInstallationRef &requested)
      const -> bot::BotOperationResult<bot::SupportedBotActions> override;
};

class BotOperationDispatcher final : public bot::BotOperationClient {
public:
  BotOperationDispatcher() = default;

  void register_endpoint(std::shared_ptr<BotOperationEndpoint> endpoint);
  [[nodiscard]] auto endpoint_count() const noexcept -> std::size_t;

  [[nodiscard]] auto supported_actions(
      const bot::BotInstallationRef &installation) const
      -> bot::BotOperationResult<bot::SupportedBotActions> override;

  auto execute(const bot::SendGroupMessageRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::SendMessageResult>> override;
  auto execute(const bot::DeleteMessageRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::DeleteMessageResult>> override;
  auto execute(const bot::SendTelegramTopicMessageRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::SendMessageResult>> override;
  auto execute(const bot::EditTelegramMessageTextRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::EditMessageTextResult>> override;
  auto execute(const bot::SendTelegramPhotoRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::SendMessageResult>> override;
  auto execute(const bot::SendTelegramMediaGroupUrlsRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::SendMessageResult>> override;
  auto execute(const bot::SendTelegramMediaGroupUploadsRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::SendMessageResult>> override;
  auto execute(const bot::FetchTelegramFileRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::FetchedTelegramFile>> override;
  auto execute(const bot::GetOneBotGroupMemberRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::OneBotGroupMember>> override;
  auto execute(const bot::GetOneBotForwardMessageRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::OneBotForwardMessage>> override;
  auto execute(const bot::ResolveOneBotGroupFileRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::ResolvedOneBotGroupFile>> override;
  auto execute(const bot::ResolveOneBotPrivateFileRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::ResolvedOneBotPrivateFile>> override;
  auto execute(const bot::PokeOneBotGroupRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::OneBotGroupPokeResult>> override;

private:
  struct ExceptionClassification {
    bot::BotOperationErrorCode code;
    bool retryable;
    bot::SubmissionSafety submission_safety;
  };

  [[nodiscard]] static auto classify_exception(const std::exception &error,
                                               bool side_effecting) noexcept
      -> ExceptionClassification;

  [[nodiscard]] auto find_endpoint(const bot::BotInstallationRef &installation)
      const -> std::shared_ptr<BotOperationEndpoint>;

  template <typename Request>
  [[nodiscard]] static auto request_installation(const Request &request)
      -> const bot::BotInstallationRef & {
    if constexpr (requires { request.target.installation; }) {
      return request.target.installation;
    } else if constexpr (requires { request.target.group.installation; }) {
      return request.target.group.installation;
    } else if constexpr (requires { request.message.group.installation; }) {
      return request.message.group.installation;
    } else {
      return request.installation;
    }
  }

  template <typename Value, typename Request, typename Invoke>
  auto dispatch(const Request &request, const bot::BotAction action,
                Invoke invoke)
      -> boost::asio::awaitable<bot::BotOperationResult<Value>> {
    try {
      request.validate();
    } catch (const std::exception &error) {
      co_return bot::failed_operation<Value>(
          bot::BotOperationErrorCode::InvalidRequest, error.what());
    }

    const auto &installation = request_installation(request);
    auto endpoint = find_endpoint(installation);
    if (!endpoint) {
      co_return bot::failed_operation<Value>(
          bot::BotOperationErrorCode::RouteNotFound,
          "bot installation is not registered: " +
              installation.installation_id);
    }
    const auto actual = endpoint->installation();
    if (actual.surface != installation.surface) {
      co_return bot::failed_operation<Value>(
          bot::BotOperationErrorCode::SurfaceMismatch,
          "bot installation surface does not match the request");
    }
    const auto actions = endpoint->declared_actions();
    if (std::ranges::find(actions, action) == actions.end()) {
      co_return bot::failed_operation<Value>(
          bot::BotOperationErrorCode::UnsupportedAction,
          "bot installation does not support action: " +
              std::string{bot::action_id(action)});
    }

    try {
      co_return co_await std::invoke(std::move(invoke), *endpoint, request);
    } catch (const std::exception &error) {
      const auto classification =
          classify_exception(error, action_may_have_side_effect(action));
      co_return bot::failed_operation<Value>(
          classification.code, bot::redact_bot_diagnostic(error.what()),
          classification.retryable, classification.submission_safety);
    } catch (...) {
      const auto side_effecting = action_may_have_side_effect(action);
      co_return bot::failed_operation<Value>(
          side_effecting ? bot::BotOperationErrorCode::OutcomeUnknown
                         : bot::BotOperationErrorCode::TransportFailure,
          "bot operation failed with an unknown exception", !side_effecting,
          side_effecting ? bot::SubmissionSafety::PossiblySubmitted
                         : bot::SubmissionSafety::DefinitelyNotSubmitted);
    }
  }

  [[nodiscard]] static constexpr auto action_may_have_side_effect(
      const bot::BotAction action) -> bool {
    switch (action) {
    case bot::BotAction::SendGroupMessage:
    case bot::BotAction::DeleteMessage:
    case bot::BotAction::SendTelegramTopicMessage:
    case bot::BotAction::EditTelegramMessageText:
    case bot::BotAction::SendTelegramPhoto:
    case bot::BotAction::SendTelegramMediaGroupUrls:
    case bot::BotAction::SendTelegramMediaGroupUploads:
    case bot::BotAction::PokeOneBotGroup:
      return true;
    case bot::BotAction::FetchTelegramFile:
    case bot::BotAction::GetOneBotGroupMember:
    case bot::BotAction::GetOneBotForwardMessage:
    case bot::BotAction::ResolveOneBotGroupFile:
    case bot::BotAction::ResolveOneBotPrivateFile:
      return false;
    }
    return false;
  }

  mutable std::shared_mutex mutex_;
  std::unordered_map<std::string, std::shared_ptr<BotOperationEndpoint>>
      endpoints_{};
};

} // namespace obcx::core

#endif // OBCX_INCLUDE_CORE_BOT_OPERATION_DISPATCHER_HPP_
