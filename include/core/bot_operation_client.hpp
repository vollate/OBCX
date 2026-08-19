#pragma once

#include "core/bot_operation_contract.hpp"

#include <boost/asio/awaitable.hpp>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace obcx::bot {

struct SupportedBotActions {
  BotInstallationRef installation;
  std::vector<BotAction> actions;

  void validate() const {
    installation.validate();
    auto normalized = actions;
    std::ranges::sort(normalized,
                      [](const BotAction left, const BotAction right) {
                        return action_id(left) < action_id(right);
                      });
    if (std::ranges::adjacent_find(normalized) != normalized.end()) {
      throw std::invalid_argument(
          "SupportedBotActions contains a duplicate action");
    }
    for (const auto action : normalized) {
      if (!action_supports_surface(action, installation.surface)) {
        throw std::invalid_argument(
            "SupportedBotActions contains an action for another surface");
      }
    }
  }

  [[nodiscard]] auto supports(const BotAction action) const -> bool {
    return std::ranges::find(actions, action) != actions.end();
  }

  auto operator==(const SupportedBotActions &) const -> bool = default;
};

inline void to_json(Json &document, const SupportedBotActions &supported) {
  supported.validate();
  auto actions = supported.actions;
  std::ranges::sort(actions, [](const BotAction left, const BotAction right) {
    return action_id(left) < action_id(right);
  });
  document = {{"installation", supported.installation}, {"actions", actions}};
}

inline void from_json(const Json &document, SupportedBotActions &supported) {
  detail::require_object(document, "SupportedBotActions");
  if (!document.contains("installation") || !document.contains("actions") ||
      !document.at("actions").is_array()) {
    throw std::invalid_argument(
        "SupportedBotActions requires installation and actions");
  }
  supported.installation =
      document.at("installation").get<BotInstallationRef>();
  supported.actions = document.at("actions").get<std::vector<BotAction>>();
  supported.validate();
}

template <typename T>
[[nodiscard]] inline auto failed_operation(
    const BotOperationErrorCode code, std::string message,
    const bool retryable = false,
    const SubmissionSafety safety = SubmissionSafety::DefinitelyNotSubmitted)
    -> BotOperationResult<T> {
  message = redact_bot_diagnostic(message);
  return BotOperationResult<T>::failure({.code = code,
                                         .message = std::move(message),
                                         .retryable = retryable,
                                         .submission_safety = safety});
}

class BotOperationClient {
public:
  BotOperationClient() = default;
  BotOperationClient(const BotOperationClient &) = delete;
  auto operator=(const BotOperationClient &) -> BotOperationClient & = delete;
  BotOperationClient(BotOperationClient &&) = delete;
  auto operator=(BotOperationClient &&) -> BotOperationClient & = delete;
  virtual ~BotOperationClient() = default;

  [[nodiscard]] virtual auto supported_actions(
      const BotInstallationRef &installation) const
      -> BotOperationResult<SupportedBotActions> = 0;

  virtual auto execute(const SendGroupMessageRequest &)
      -> boost::asio::awaitable<BotOperationResult<SendMessageResult>> {
    co_return unsupported<SendMessageResult>(BotAction::SendGroupMessage);
  }

  virtual auto execute(const DeleteMessageRequest &)
      -> boost::asio::awaitable<BotOperationResult<DeleteMessageResult>> {
    co_return unsupported<DeleteMessageResult>(BotAction::DeleteMessage);
  }

  virtual auto execute(const SendTelegramTopicMessageRequest &)
      -> boost::asio::awaitable<BotOperationResult<SendMessageResult>> {
    co_return unsupported<SendMessageResult>(
        BotAction::SendTelegramTopicMessage);
  }

  virtual auto execute(const EditTelegramMessageTextRequest &)
      -> boost::asio::awaitable<BotOperationResult<EditMessageTextResult>> {
    co_return unsupported<EditMessageTextResult>(
        BotAction::EditTelegramMessageText);
  }

  virtual auto execute(const SendTelegramPhotoRequest &)
      -> boost::asio::awaitable<BotOperationResult<SendMessageResult>> {
    co_return unsupported<SendMessageResult>(BotAction::SendTelegramPhoto);
  }

  virtual auto execute(const SendTelegramMediaGroupUrlsRequest &)
      -> boost::asio::awaitable<BotOperationResult<SendMessageResult>> {
    co_return unsupported<SendMessageResult>(
        BotAction::SendTelegramMediaGroupUrls);
  }

  virtual auto execute(const SendTelegramMediaGroupUploadsRequest &)
      -> boost::asio::awaitable<BotOperationResult<SendMessageResult>> {
    co_return unsupported<SendMessageResult>(
        BotAction::SendTelegramMediaGroupUploads);
  }

  virtual auto execute(const FetchTelegramFileRequest &)
      -> boost::asio::awaitable<BotOperationResult<FetchedTelegramFile>> {
    co_return unsupported<FetchedTelegramFile>(BotAction::FetchTelegramFile);
  }

  virtual auto execute(const GetOneBotGroupMemberRequest &)
      -> boost::asio::awaitable<BotOperationResult<OneBotGroupMember>> {
    co_return unsupported<OneBotGroupMember>(BotAction::GetOneBotGroupMember);
  }

  virtual auto execute(const GetOneBotForwardMessageRequest &)
      -> boost::asio::awaitable<BotOperationResult<OneBotForwardMessage>> {
    co_return unsupported<OneBotForwardMessage>(
        BotAction::GetOneBotForwardMessage);
  }

  virtual auto execute(const ResolveOneBotGroupFileRequest &)
      -> boost::asio::awaitable<BotOperationResult<ResolvedOneBotGroupFile>> {
    co_return unsupported<ResolvedOneBotGroupFile>(
        BotAction::ResolveOneBotGroupFile);
  }

  virtual auto execute(const ResolveOneBotPrivateFileRequest &)
      -> boost::asio::awaitable<BotOperationResult<ResolvedOneBotPrivateFile>> {
    co_return unsupported<ResolvedOneBotPrivateFile>(
        BotAction::ResolveOneBotPrivateFile);
  }

  virtual auto execute(const PokeOneBotGroupRequest &)
      -> boost::asio::awaitable<BotOperationResult<OneBotGroupPokeResult>> {
    co_return unsupported<OneBotGroupPokeResult>(BotAction::PokeOneBotGroup);
  }

protected:
  template <typename T>
  [[nodiscard]] static auto unsupported(const BotAction action)
      -> BotOperationResult<T> {
    return failed_operation<T>(BotOperationErrorCode::UnsupportedAction,
                               "bot action is not implemented: " +
                                   std::string{action_id(action)});
  }
};

} // namespace obcx::bot
