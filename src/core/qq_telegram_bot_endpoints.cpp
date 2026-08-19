#include "core/qq_telegram_bot_endpoints.hpp"

#include "core/bot_operation_response_parser.hpp"
#include "interfaces/bot.hpp"
#include "interfaces/qq_bot.hpp"
#include "interfaces/telegram_bot.hpp"

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace obcx::core {
namespace {

auto provider_id(const bot::Json &document, const std::string_view key)
    -> std::optional<std::string> {
  const auto field = std::string{key};
  if (!document.is_object() || !document.contains(field)) {
    return std::nullopt;
  }
  const auto &value = document.at(field);
  if (value.is_string()) {
    const auto id = value.get<std::string>();
    return id.empty() ? std::nullopt : std::optional{id};
  }
  if (value.is_number_integer()) {
    return std::to_string(value.get<std::int64_t>());
  }
  if (value.is_number_unsigned()) {
    return std::to_string(value.get<std::uint64_t>());
  }
  return std::nullopt;
}

template <typename T>
auto provider_failure(const bot::BotOperationResult<bot::Json> &parsed)
    -> bot::BotOperationResult<T> {
  return bot::BotOperationResult<T>::failure(*parsed.error);
}

template <typename T>
auto malformed_side_effect(std::string message) -> bot::BotOperationResult<T> {
  return bot::failed_operation<T>(bot::BotOperationErrorCode::MalformedResponse,
                                  std::move(message), false,
                                  bot::SubmissionSafety::PossiblySubmitted);
}

template <typename T>
auto malformed_read(std::string message) -> bot::BotOperationResult<T> {
  return bot::failed_operation<T>(
      bot::BotOperationErrorCode::MalformedResponse, std::move(message), true,
      bot::SubmissionSafety::DefinitelyNotSubmitted);
}

auto optional_string(const bot::Json &document, const std::string_view key)
    -> std::string {
  const auto field = std::string{key};
  return document.is_object() && document.contains(field) &&
                 document.at(field).is_string()
             ? document.at(field).get<std::string>()
             : std::string{};
}

auto telegram_send_result(const bot::BotOperationResult<bot::Json> &parsed,
                          const bot::GroupTarget &target,
                          const std::string_view operation)
    -> bot::BotOperationResult<bot::SendMessageResult> {
  if (!parsed.ok()) {
    return provider_failure<bot::SendMessageResult>(parsed);
  }
  std::vector<bot::BotMessageRef> messages;
  const auto append = [&](const bot::Json &message) -> bool {
    const auto id = provider_id(message, "message_id");
    if (!id.has_value()) {
      return false;
    }
    messages.push_back({.group = target, .native_message_id = *id});
    return true;
  };
  if (parsed.value->is_object()) {
    if (!append(*parsed.value)) {
      return malformed_side_effect<bot::SendMessageResult>(
          std::string{operation} + " response is missing message_id");
    }
  } else if (parsed.value->is_array() && !parsed.value->empty()) {
    for (const auto &message : *parsed.value) {
      if (!message.is_object() || !append(message)) {
        return malformed_side_effect<bot::SendMessageResult>(
            std::string{operation} +
            " response contains an invalid message result");
      }
    }
  } else {
    return malformed_side_effect<bot::SendMessageResult>(
        std::string{operation} + " response has an invalid result");
  }
  return bot::BotOperationResult<bot::SendMessageResult>::success(
      {.messages = std::move(messages)});
}

auto telegram_entities(const std::vector<bot::TelegramTextEntity> &entities)
    -> std::vector<TelegramTextEntity> {
  std::vector<TelegramTextEntity> converted;
  converted.reserve(entities.size());
  for (const auto &entity : entities) {
    converted.push_back({.type = entity.type,
                         .offset = entity.offset,
                         .length = entity.length});
  }
  return converted;
}

class OneBot11OperationEndpoint final : public BotOperationEndpoint {
public:
  OneBot11OperationEndpoint(std::string installation_id,
                            std::shared_ptr<IBot> bot,
                            std::shared_ptr<IQQBot> onebot)
      : installation_{.installation_id = std::move(installation_id),
                      .surface = bot::BotSurface::OneBot11Qq},
        bot_(std::move(bot)), onebot_(std::move(onebot)) {
    installation_.validate();
    if (!bot_ || !onebot_) {
      throw std::invalid_argument("onebot11.qq wrapper requires a live IQQBot");
    }
  }

  [[nodiscard]] auto installation() const -> bot::BotInstallationRef override {
    return installation_;
  }

  [[nodiscard]] auto declared_actions() const
      -> std::vector<bot::BotAction> override {
    return {bot::BotAction::SendGroupMessage,
            bot::BotAction::DeleteMessage,
            bot::BotAction::GetOneBotGroupMember,
            bot::BotAction::GetOneBotForwardMessage,
            bot::BotAction::ResolveOneBotGroupFile,
            bot::BotAction::ResolveOneBotPrivateFile,
            bot::BotAction::PokeOneBotGroup};
  }

  auto execute(const bot::SendGroupMessageRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::SendMessageResult>> override {
    const auto response = co_await bot_->send_group_message(
        request.target.native_group_id, request.message);
    const auto parsed = parse_onebot11_operation_response(response, true);
    if (!parsed.ok()) {
      co_return provider_failure<bot::SendMessageResult>(parsed);
    }
    const auto message_id = provider_id(*parsed.value, "message_id");
    if (!message_id.has_value()) {
      co_return malformed_side_effect<bot::SendMessageResult>(
          "OneBot send response is missing message_id");
    }
    co_return bot::BotOperationResult<bot::SendMessageResult>::success(
        {.messages = {
             {.group = request.target, .native_message_id = *message_id}}});
  }

  auto execute(const bot::DeleteMessageRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::DeleteMessageResult>> override {
    const auto response =
        co_await bot_->delete_message(request.message.native_message_id);
    const auto parsed = parse_onebot11_operation_response(response, true);
    if (!parsed.ok()) {
      co_return provider_failure<bot::DeleteMessageResult>(parsed);
    }
    co_return bot::BotOperationResult<bot::DeleteMessageResult>::success(
        {.message = request.message});
  }

  auto execute(const bot::GetOneBotGroupMemberRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::OneBotGroupMember>> override {
    const auto response = co_await bot_->get_group_member_info(
        request.target.native_group_id, request.user_id, request.no_cache);
    const auto parsed = parse_onebot11_operation_response(response, false);
    if (!parsed.ok()) {
      co_return provider_failure<bot::OneBotGroupMember>(parsed);
    }
    const auto user_id = provider_id(*parsed.value, "user_id");
    if (!user_id.has_value()) {
      co_return malformed_read<bot::OneBotGroupMember>(
          "OneBot member response is missing user_id");
    }
    co_return bot::BotOperationResult<bot::OneBotGroupMember>::success(
        {.target = request.target,
         .user_id = *user_id,
         .nickname = optional_string(*parsed.value, "nickname"),
         .card = optional_string(*parsed.value, "card"),
         .title = optional_string(*parsed.value, "title")});
  }

  auto execute(const bot::GetOneBotForwardMessageRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::OneBotForwardMessage>> override {
    const auto response = co_await onebot_->get_forward_msg(request.forward_id);
    const auto parsed = parse_onebot11_operation_response(response, false);
    if (!parsed.ok()) {
      co_return provider_failure<bot::OneBotForwardMessage>(parsed);
    }
    if (!parsed.value->is_object() || !parsed.value->contains("messages") ||
        !parsed.value->at("messages").is_array()) {
      co_return malformed_read<bot::OneBotForwardMessage>(
          "OneBot forward response is missing messages");
    }
    bot::OneBotForwardMessage result{.installation = installation_,
                                     .forward_id = request.forward_id,
                                     .messages = parsed.value->at("messages")};
    try {
      result.validate();
    } catch (const std::exception &error) {
      co_return malformed_read<bot::OneBotForwardMessage>(error.what());
    }
    co_return bot::BotOperationResult<bot::OneBotForwardMessage>::success(
        std::move(result));
  }

  auto execute(const bot::ResolveOneBotGroupFileRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::ResolvedOneBotGroupFile>> override {
    const auto response = co_await onebot_->get_group_file_url(
        request.target.native_group_id, request.file_id);
    const auto parsed = parse_onebot11_operation_response(response, false);
    if (!parsed.ok()) {
      co_return provider_failure<bot::ResolvedOneBotGroupFile>(parsed);
    }
    const auto url = optional_string(*parsed.value, "url");
    if (url.empty()) {
      co_return malformed_read<bot::ResolvedOneBotGroupFile>(
          "OneBot group-file response is missing url");
    }
    co_return bot::BotOperationResult<bot::ResolvedOneBotGroupFile>::success(
        {.target = request.target, .file_id = request.file_id, .url = url});
  }

  auto execute(const bot::ResolveOneBotPrivateFileRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::ResolvedOneBotPrivateFile>> override {
    const auto response = co_await onebot_->get_private_file_url(
        request.user_id, request.file_id);
    const auto parsed = parse_onebot11_operation_response(response, false);
    if (!parsed.ok()) {
      co_return provider_failure<bot::ResolvedOneBotPrivateFile>(parsed);
    }
    const auto url = optional_string(*parsed.value, "url");
    if (url.empty()) {
      co_return malformed_read<bot::ResolvedOneBotPrivateFile>(
          "OneBot private-file response is missing url");
    }
    co_return bot::BotOperationResult<bot::ResolvedOneBotPrivateFile>::success(
        {.installation = installation_,
         .user_id = request.user_id,
         .file_id = request.file_id,
         .url = url});
  }

  auto execute(const bot::PokeOneBotGroupRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::OneBotGroupPokeResult>> override {
    const auto response = co_await onebot_->group_poke(
        request.target.native_group_id, request.user_id);
    const auto parsed = parse_onebot11_operation_response(response, true);
    if (!parsed.ok()) {
      co_return provider_failure<bot::OneBotGroupPokeResult>(parsed);
    }
    co_return bot::BotOperationResult<bot::OneBotGroupPokeResult>::success(
        {.target = request.target, .user_id = request.user_id});
  }

private:
  bot::BotInstallationRef installation_;
  std::shared_ptr<IBot> bot_;
  std::shared_ptr<IQQBot> onebot_;
};

class TelegramOperationEndpoint final : public BotOperationEndpoint {
public:
  TelegramOperationEndpoint(
      std::string installation_id, std::shared_ptr<IBot> bot,
      std::shared_ptr<ITelegramBot> telegram,
      std::shared_ptr<ITelegramMediaGroupUploader> uploader)
      : installation_{.installation_id = std::move(installation_id),
                      .surface = bot::BotSurface::TelegramBotApi},
        bot_(std::move(bot)), telegram_(std::move(telegram)),
        uploader_(std::move(uploader)) {
    installation_.validate();
    if (!bot_ || !telegram_) {
      throw std::invalid_argument(
          "telegram.bot_api wrapper requires a live ITelegramBot");
    }
  }

  [[nodiscard]] auto installation() const -> bot::BotInstallationRef override {
    return installation_;
  }

  [[nodiscard]] auto declared_actions() const
      -> std::vector<bot::BotAction> override {
    auto actions = std::vector{
        bot::BotAction::SendGroupMessage,
        bot::BotAction::DeleteMessage,
        bot::BotAction::SendTelegramTopicMessage,
        bot::BotAction::EditTelegramMessageText,
        bot::BotAction::SendTelegramPhoto,
        bot::BotAction::SendTelegramMediaGroupUrls,
        bot::BotAction::FetchTelegramFile,
    };
    if (uploader_) {
      actions.push_back(bot::BotAction::SendTelegramMediaGroupUploads);
    }
    return actions;
  }

  auto execute(const bot::SendGroupMessageRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::SendMessageResult>> override {
    const auto response = co_await bot_->send_group_message(
        request.target.native_group_id, request.message);
    const auto parsed = parse_telegram_operation_response(response, true);
    if (!parsed.ok()) {
      co_return provider_failure<bot::SendMessageResult>(parsed);
    }
    const auto message_id = provider_id(*parsed.value, "message_id");
    if (!message_id.has_value()) {
      co_return malformed_side_effect<bot::SendMessageResult>(
          "Telegram send response is missing message_id");
    }
    co_return bot::BotOperationResult<bot::SendMessageResult>::success(
        {.messages = {
             {.group = request.target, .native_message_id = *message_id}}});
  }

  auto execute(const bot::DeleteMessageRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::DeleteMessageResult>> override {
    const auto provider_reference = request.message.group.native_group_id +
                                    ":" + request.message.native_message_id;
    const auto response = co_await bot_->delete_message(provider_reference);
    const auto parsed = parse_telegram_operation_response(response, true);
    if (!parsed.ok()) {
      co_return provider_failure<bot::DeleteMessageResult>(parsed);
    }
    if (!parsed.value->is_boolean() || !parsed.value->get<bool>()) {
      co_return malformed_side_effect<bot::DeleteMessageResult>(
          "Telegram delete response does not confirm deletion");
    }
    co_return bot::BotOperationResult<bot::DeleteMessageResult>::success(
        {.message = request.message});
  }

  auto execute(const bot::SendTelegramTopicMessageRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::SendMessageResult>> override {
    const auto response = co_await telegram_->send_topic_message(
        request.target.group.native_group_id, request.target.topic_id,
        request.message);
    const auto parsed = parse_telegram_operation_response(response, true);
    if (!parsed.ok()) {
      co_return provider_failure<bot::SendMessageResult>(parsed);
    }
    const auto message_id = provider_id(*parsed.value, "message_id");
    if (!message_id.has_value()) {
      co_return malformed_side_effect<bot::SendMessageResult>(
          "Telegram topic send response is missing message_id");
    }
    co_return bot::BotOperationResult<bot::SendMessageResult>::success(
        {.messages = {{.group = request.target.group,
                       .native_message_id = *message_id}}});
  }

  auto execute(const bot::EditTelegramMessageTextRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::EditMessageTextResult>> override {
    const auto response = co_await telegram_->edit_message_text(
        request.message.group.native_group_id,
        request.message.native_message_id, request.text, request.parse_mode);
    const auto parsed = parse_telegram_operation_response(response, true);
    if (!parsed.ok()) {
      co_return provider_failure<bot::EditMessageTextResult>(parsed);
    }
    const auto message_id = provider_id(*parsed.value, "message_id");
    if (!message_id.has_value() ||
        *message_id != request.message.native_message_id) {
      co_return malformed_side_effect<bot::EditMessageTextResult>(
          "Telegram edit response has no matching message_id");
    }
    co_return bot::BotOperationResult<bot::EditMessageTextResult>::success(
        {.message = request.message});
  }

  auto execute(const bot::SendTelegramPhotoRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::SendMessageResult>> override {
    const auto response = co_await telegram_->send_group_photo_with_entities(
        request.target.native_group_id, request.photo, request.caption,
        telegram_entities(request.caption_entities));
    co_return telegram_send_result(
        parse_telegram_operation_response(response, true), request.target,
        "Telegram photo send");
  }

  auto execute(const bot::SendTelegramMediaGroupUrlsRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::SendMessageResult>> override {
    std::vector<std::pair<std::string, std::string>> media;
    media.reserve(request.media.size());
    for (const auto &item : request.media) {
      media.emplace_back(item.type, item.source);
    }
    const auto reply = request.reply_to.has_value()
                           ? std::optional{request.reply_to->native_message_id}
                           : std::nullopt;
    const auto response = co_await telegram_->send_media_group_with_entities(
        request.target.native_group_id, media, request.caption,
        request.topic_id, reply, telegram_entities(request.caption_entities));
    co_return telegram_send_result(
        parse_telegram_operation_response(response, true), request.target,
        "Telegram URL media-group send");
  }

  auto execute(const bot::SendTelegramMediaGroupUploadsRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::SendMessageResult>> override {
    if (!uploader_) {
      co_return bot::failed_operation<bot::SendMessageResult>(
          bot::BotOperationErrorCode::UnsupportedAction,
          "Telegram multipart media-group upload is unavailable");
    }
    std::vector<TelegramMediaUpload> media;
    media.reserve(request.media.size());
    for (const auto &item : request.media) {
      media.push_back({.type = item.type,
                       .filename = item.filename,
                       .mime_type = item.mime_type,
                       .data = std::string{
                           reinterpret_cast<const char *>(item.bytes.data()),
                           item.bytes.size()}});
    }
    const auto reply = request.reply_to.has_value()
                           ? std::optional{request.reply_to->native_message_id}
                           : std::nullopt;
    const auto response =
        co_await uploader_->send_media_group_uploads_with_entities(
            request.target.native_group_id, media, request.caption,
            request.topic_id, reply,
            telegram_entities(request.caption_entities));
    co_return telegram_send_result(
        parse_telegram_operation_response(response, true), request.target,
        "Telegram multipart media-group send");
  }

  auto execute(const bot::FetchTelegramFileRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::FetchedTelegramFile>> override {
    const MediaFileInfo file{.file_id = request.file.file_id,
                             .file_unique_id = request.file.file_unique_id,
                             .file_type = request.file.file_type,
                             .file_size = request.file.file_size,
                             .mime_type = request.file.mime_type,
                             .file_name = request.file.file_name};
    const auto download_url = co_await telegram_->get_media_download_url(file);
    if (!download_url.has_value() || download_url->empty()) {
      co_return bot::failed_operation<bot::FetchedTelegramFile>(
          bot::BotOperationErrorCode::ProviderRejected,
          "Telegram did not resolve the requested file");
    }
    const auto content =
        co_await telegram_->download_file_content(*download_url);
    if (content.size() > request.maximum_bytes) {
      co_return bot::failed_operation<bot::FetchedTelegramFile>(
          bot::BotOperationErrorCode::MediaTooLarge,
          "Telegram file exceeds the requested byte bound");
    }
    if (content.empty()) {
      co_return bot::failed_operation<bot::FetchedTelegramFile>(
          bot::BotOperationErrorCode::MalformedResponse,
          "Telegram returned an empty file");
    }
    std::vector<std::uint8_t> bytes;
    bytes.reserve(content.size());
    for (const auto value : content) {
      bytes.push_back(
          static_cast<std::uint8_t>(static_cast<unsigned char>(value)));
    }
    co_return bot::BotOperationResult<bot::FetchedTelegramFile>::success(
        {.installation = installation_,
         .file = request.file,
         .bytes = std::move(bytes)});
  }

private:
  bot::BotInstallationRef installation_;
  std::shared_ptr<IBot> bot_;
  std::shared_ptr<ITelegramBot> telegram_;
  std::shared_ptr<ITelegramMediaGroupUploader> uploader_;
};

} // namespace

auto bot_surface_for_config_type(const std::string_view type)
    -> bot::BotSurface {
  if (type == "telegram") {
    return bot::BotSurface::TelegramBotApi;
  }
  if (type == "qq") {
    return bot::BotSurface::OneBot11Qq;
  }
  throw std::invalid_argument("unsupported bot operation config type: " +
                              std::string{type});
}

auto make_existing_bot_operation_endpoint(
    std::string installation_id, const std::string_view configured_type,
    std::shared_ptr<IBot> live_bot) -> std::shared_ptr<BotOperationEndpoint> {
  if (!live_bot) {
    throw std::invalid_argument("bot operation wrapper requires a live bot");
  }
  const auto surface = bot_surface_for_config_type(configured_type);
  if (surface == bot::BotSurface::TelegramBotApi) {
    auto telegram = std::dynamic_pointer_cast<ITelegramBot>(live_bot);
    if (!telegram) {
      throw std::invalid_argument(
          "configured Telegram bot does not implement ITelegramBot");
    }
    auto uploader =
        std::dynamic_pointer_cast<ITelegramMediaGroupUploader>(live_bot);
    return std::make_shared<TelegramOperationEndpoint>(
        std::move(installation_id), std::move(live_bot), std::move(telegram),
        std::move(uploader));
  }

  auto onebot = std::dynamic_pointer_cast<IQQBot>(live_bot);
  if (!onebot) {
    throw std::invalid_argument("configured QQ bot does not implement IQQBot");
  }
  return std::make_shared<OneBot11OperationEndpoint>(
      std::move(installation_id), std::move(live_bot), std::move(onebot));
}

void register_existing_bot_operation_endpoint(
    QQTelegramOperationDispatcher &dispatcher, std::string installation_id,
    const std::string_view configured_type, std::shared_ptr<IBot> live_bot) {
  dispatcher.register_endpoint(make_existing_bot_operation_endpoint(
      std::move(installation_id), configured_type, std::move(live_bot)));
}

} // namespace obcx::core
