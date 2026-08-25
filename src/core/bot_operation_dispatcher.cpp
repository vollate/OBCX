#include "core/bot_operation_dispatcher.hpp"

#include "network/http_client.hpp"

#include <boost/asio/awaitable.hpp>

#include <mutex>
#include <stdexcept>
#include <utility>

namespace obcx::core {

auto BotOperationDispatcher::classify_exception(
    const std::exception &error, const bool side_effecting) noexcept
    -> ExceptionClassification {
  if (!side_effecting) {
    return {.code = bot::BotOperationErrorCode::TransportFailure,
            .retryable = true,
            .submission_safety = bot::SubmissionSafety::DefinitelyNotSubmitted};
  }

  const auto *http_error =
      dynamic_cast<const network::HttpClientError *>(&error);
  if (http_error != nullptr &&
      http_error->submission_state() ==
          network::HttpRequestSubmissionState::DefinitelyNotSubmitted) {
    return {.code = bot::BotOperationErrorCode::TransportFailure,
            .retryable = true,
            .submission_safety = bot::SubmissionSafety::DefinitelyNotSubmitted};
  }

  return {.code = bot::BotOperationErrorCode::OutcomeUnknown,
          .retryable = false,
          .submission_safety = bot::SubmissionSafety::PossiblySubmitted};
}

auto BotOperationEndpoint::supported_actions(
    const bot::BotInstallationRef &requested) const
    -> bot::BotOperationResult<bot::SupportedBotActions> {
  try {
    requested.validate();
  } catch (const std::exception &error) {
    return bot::failed_operation<bot::SupportedBotActions>(
        bot::BotOperationErrorCode::InvalidRequest, error.what());
  }

  const auto actual = installation();
  if (requested.installation_id != actual.installation_id) {
    return bot::failed_operation<bot::SupportedBotActions>(
        bot::BotOperationErrorCode::RouteNotFound,
        "bot endpoint does not own the requested installation");
  }
  if (requested.surface != actual.surface) {
    return bot::failed_operation<bot::SupportedBotActions>(
        bot::BotOperationErrorCode::SurfaceMismatch,
        "bot endpoint surface does not match the request");
  }

  bot::SupportedBotActions supported{.installation = actual,
                                     .actions = declared_actions()};
  try {
    supported.validate();
  } catch (const std::exception &error) {
    return bot::failed_operation<bot::SupportedBotActions>(
        bot::BotOperationErrorCode::InvalidRequest, error.what());
  }
  return bot::BotOperationResult<bot::SupportedBotActions>::success(
      std::move(supported));
}

void BotOperationDispatcher::register_endpoint(
    std::shared_ptr<BotOperationEndpoint> endpoint) {
  if (!endpoint) {
    throw std::invalid_argument("bot operation endpoint cannot be null");
  }
  const auto installation = endpoint->installation();
  installation.validate();
  auto supported = endpoint->supported_actions(installation);
  supported.validate();
  if (!supported.ok()) {
    const auto message = supported.error ? supported.error.value().message
                                         : "missing typed endpoint error";
    throw std::invalid_argument("bot operation endpoint actions are invalid: " +
                                message);
  }

  std::unique_lock lock(mutex_);
  const auto [_, inserted] =
      endpoints_.emplace(installation.installation_id, std::move(endpoint));
  if (!inserted) {
    throw std::invalid_argument("duplicate bot installation id: " +
                                installation.installation_id);
  }
}

auto BotOperationDispatcher::endpoint_count() const noexcept -> std::size_t {
  std::shared_lock lock(mutex_);
  return endpoints_.size();
}

auto BotOperationDispatcher::find_endpoint(
    const bot::BotInstallationRef &installation) const
    -> std::shared_ptr<BotOperationEndpoint> {
  std::shared_lock lock(mutex_);
  const auto found = endpoints_.find(installation.installation_id);
  return found == endpoints_.end() ? nullptr : found->second;
}

auto BotOperationDispatcher::supported_actions(
    const bot::BotInstallationRef &installation) const
    -> bot::BotOperationResult<bot::SupportedBotActions> {
  try {
    installation.validate();
  } catch (const std::exception &error) {
    return bot::failed_operation<bot::SupportedBotActions>(
        bot::BotOperationErrorCode::InvalidRequest, error.what());
  }
  auto endpoint = find_endpoint(installation);
  if (!endpoint) {
    return bot::failed_operation<bot::SupportedBotActions>(
        bot::BotOperationErrorCode::RouteNotFound,
        "bot installation is not registered: " + installation.installation_id);
  }
  return endpoint->supported_actions(installation);
}

auto BotOperationDispatcher::execute(
    const bot::SendGroupMessageRequest &request)
    -> boost::asio::awaitable<bot::BotOperationResult<bot::SendMessageResult>> {
  co_return co_await dispatch<bot::SendMessageResult>(
      request, bot::BotAction::SendGroupMessage,
      [](BotOperationEndpoint &endpoint,
         const bot::SendGroupMessageRequest &operation) {
        return endpoint.execute(operation);
      });
}

auto BotOperationDispatcher::execute(const bot::DeleteMessageRequest &request)
    -> boost::asio::awaitable<
        bot::BotOperationResult<bot::DeleteMessageResult>> {
  co_return co_await dispatch<bot::DeleteMessageResult>(
      request, bot::BotAction::DeleteMessage,
      [](BotOperationEndpoint &endpoint,
         const bot::DeleteMessageRequest &operation) {
        return endpoint.execute(operation);
      });
}

auto BotOperationDispatcher::execute(
    const bot::SendTelegramTopicMessageRequest &request)
    -> boost::asio::awaitable<bot::BotOperationResult<bot::SendMessageResult>> {
  co_return co_await dispatch<bot::SendMessageResult>(
      request, bot::BotAction::SendTelegramTopicMessage,
      [](BotOperationEndpoint &endpoint,
         const bot::SendTelegramTopicMessageRequest &operation) {
        return endpoint.execute(operation);
      });
}

auto BotOperationDispatcher::execute(
    const bot::EditTelegramMessageTextRequest &request)
    -> boost::asio::awaitable<
        bot::BotOperationResult<bot::EditMessageTextResult>> {
  co_return co_await dispatch<bot::EditMessageTextResult>(
      request, bot::BotAction::EditTelegramMessageText,
      [](BotOperationEndpoint &endpoint,
         const bot::EditTelegramMessageTextRequest &operation) {
        return endpoint.execute(operation);
      });
}

auto BotOperationDispatcher::execute(
    const bot::SendTelegramPhotoRequest &request)
    -> boost::asio::awaitable<bot::BotOperationResult<bot::SendMessageResult>> {
  co_return co_await dispatch<bot::SendMessageResult>(
      request, bot::BotAction::SendTelegramPhoto,
      [](BotOperationEndpoint &endpoint,
         const bot::SendTelegramPhotoRequest &operation) {
        return endpoint.execute(operation);
      });
}

auto BotOperationDispatcher::execute(
    const bot::SendTelegramMediaGroupUrlsRequest &request)
    -> boost::asio::awaitable<bot::BotOperationResult<bot::SendMessageResult>> {
  co_return co_await dispatch<bot::SendMessageResult>(
      request, bot::BotAction::SendTelegramMediaGroupUrls,
      [](BotOperationEndpoint &endpoint,
         const bot::SendTelegramMediaGroupUrlsRequest &operation) {
        return endpoint.execute(operation);
      });
}

auto BotOperationDispatcher::execute(
    const bot::SendTelegramMediaGroupUploadsRequest &request)
    -> boost::asio::awaitable<bot::BotOperationResult<bot::SendMessageResult>> {
  co_return co_await dispatch<bot::SendMessageResult>(
      request, bot::BotAction::SendTelegramMediaGroupUploads,
      [](BotOperationEndpoint &endpoint,
         const bot::SendTelegramMediaGroupUploadsRequest &operation) {
        return endpoint.execute(operation);
      });
}

auto BotOperationDispatcher::execute(
    const bot::FetchTelegramFileRequest &request)
    -> boost::asio::awaitable<
        bot::BotOperationResult<bot::FetchedTelegramFile>> {
  co_return co_await dispatch<bot::FetchedTelegramFile>(
      request, bot::BotAction::FetchTelegramFile,
      [](BotOperationEndpoint &endpoint,
         const bot::FetchTelegramFileRequest &operation) {
        return endpoint.execute(operation);
      });
}

auto BotOperationDispatcher::execute(
    const bot::GetOneBotGroupMemberRequest &request)
    -> boost::asio::awaitable<bot::BotOperationResult<bot::OneBotGroupMember>> {
  co_return co_await dispatch<bot::OneBotGroupMember>(
      request, bot::BotAction::GetOneBotGroupMember,
      [](BotOperationEndpoint &endpoint,
         const bot::GetOneBotGroupMemberRequest &operation) {
        return endpoint.execute(operation);
      });
}

auto BotOperationDispatcher::execute(
    const bot::GetOneBotForwardMessageRequest &request)
    -> boost::asio::awaitable<
        bot::BotOperationResult<bot::OneBotForwardMessage>> {
  co_return co_await dispatch<bot::OneBotForwardMessage>(
      request, bot::BotAction::GetOneBotForwardMessage,
      [](BotOperationEndpoint &endpoint,
         const bot::GetOneBotForwardMessageRequest &operation) {
        return endpoint.execute(operation);
      });
}

auto BotOperationDispatcher::execute(
    const bot::ResolveOneBotGroupFileRequest &request)
    -> boost::asio::awaitable<
        bot::BotOperationResult<bot::ResolvedOneBotGroupFile>> {
  co_return co_await dispatch<bot::ResolvedOneBotGroupFile>(
      request, bot::BotAction::ResolveOneBotGroupFile,
      [](BotOperationEndpoint &endpoint,
         const bot::ResolveOneBotGroupFileRequest &operation) {
        return endpoint.execute(operation);
      });
}

auto BotOperationDispatcher::execute(
    const bot::ResolveOneBotPrivateFileRequest &request)
    -> boost::asio::awaitable<
        bot::BotOperationResult<bot::ResolvedOneBotPrivateFile>> {
  co_return co_await dispatch<bot::ResolvedOneBotPrivateFile>(
      request, bot::BotAction::ResolveOneBotPrivateFile,
      [](BotOperationEndpoint &endpoint,
         const bot::ResolveOneBotPrivateFileRequest &operation) {
        return endpoint.execute(operation);
      });
}

auto BotOperationDispatcher::execute(const bot::PokeOneBotGroupRequest &request)
    -> boost::asio::awaitable<
        bot::BotOperationResult<bot::OneBotGroupPokeResult>> {
  co_return co_await dispatch<bot::OneBotGroupPokeResult>(
      request, bot::BotAction::PokeOneBotGroup,
      [](BotOperationEndpoint &endpoint,
         const bot::PokeOneBotGroupRequest &operation) {
        return endpoint.execute(operation);
      });
}

} // namespace obcx::core
