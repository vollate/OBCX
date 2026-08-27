#include "core/bot_operation_components.hpp"

#include "core/bot_installation_assembler.hpp"
#include "core/bot_operation_response_parser.hpp"
#include "onebot11/adapter/protocol_adapter.hpp"
#include "telegram/adapter/protocol_adapter.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdint>
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
  if (parsed.error) {
    return bot::BotOperationResult<T>::failure(parsed.error.value());
  }
  return bot::failed_operation<T>(
      bot::BotOperationErrorCode::MalformedResponse,
      "provider failure did not contain a typed error");
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

auto parsed_value(const bot::BotOperationResult<bot::Json> &parsed)
    -> const bot::Json & {
  if (!parsed.value) {
    throw std::logic_error("successful provider parse has no value");
  }
  return parsed.value.value();
}

auto optional_string(const bot::Json &document, const std::string_view key)
    -> std::string {
  const auto field = std::string{key};
  return document.is_object() && document.contains(field) &&
                 document.at(field).is_string()
             ? document.at(field).get<std::string>()
             : std::string{};
}

enum class TelegramSendResultShape : std::uint8_t {
  SingleMessage,
  MediaGroup,
};

auto telegram_send_result(const bot::BotOperationResult<bot::Json> &parsed,
                          const bot::GroupTarget &target,
                          const std::string_view operation,
                          const TelegramSendResultShape expected_shape,
                          const std::size_t expected_count = 1U)
    -> bot::BotOperationResult<bot::SendMessageResult> {
  if (!parsed.ok()) {
    return provider_failure<bot::SendMessageResult>(parsed);
  }
  if (!parsed.value) {
    return malformed_side_effect<bot::SendMessageResult>(
        std::string{operation} + " response has no result");
  }
  const auto &value = parsed.value.value();
  std::vector<bot::BotMessageRef> messages;
  const auto append = [&](const bot::Json &message) {
    const auto id = provider_id(message, "message_id");
    if (!id) {
      return false;
    }
    messages.push_back({.group = target, .native_message_id = *id});
    return true;
  };
  if (expected_shape == TelegramSendResultShape::SingleMessage &&
      value.is_object()) {
    if (!append(value)) {
      return malformed_side_effect<bot::SendMessageResult>(
          std::string{operation} + " response is missing message_id");
    }
  } else if (expected_shape == TelegramSendResultShape::MediaGroup &&
             value.is_array() && value.size() == expected_count) {
    for (const auto &message : value) {
      if (!message.is_object() || !append(message)) {
        return malformed_side_effect<bot::SendMessageResult>(
            std::string{operation} +
            " response contains an invalid message result");
      }
    }
  } else {
    return malformed_side_effect<bot::SendMessageResult>(
        std::string{operation} + " response has an unexpected result shape");
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

} // namespace

class OneBot11OperationsComponent::Endpoint final
    : public BotOperationEndpoint {
public:
  explicit Endpoint(std::string installation_id)
      : installation_{.installation_id = std::move(installation_id),
                      .surface = bot::BotSurface::OneBot11Qq} {
    installation_.validate();
  }

  void configure(std::shared_ptr<adapter::onebot11::ProtocolAdapter> protocol,
                 std::shared_ptr<OneBot11Transport> transport) {
    if (protocol == nullptr || transport == nullptr || protocol_ != nullptr ||
        transport_ != nullptr) {
      throw BotComponentRuntimeError(
          "OneBot operations require one protocol and transport");
    }
    protocol_ = std::move(protocol);
    transport_ = std::move(transport);
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
    const auto echo = next_echo();
    const auto response = co_await transport().send_action(
        protocol().serialize_send_group_message_request(
            request.target.native_group_id, request.message, echo),
        echo);
    const auto parsed = parse_onebot11_operation_response(response, true);
    if (!parsed.ok()) {
      co_return provider_failure<bot::SendMessageResult>(parsed);
    }
    const auto message_id = provider_id(parsed_value(parsed), "message_id");
    if (!message_id) {
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
    const auto echo = next_echo();
    const auto response = co_await transport().send_action(
        protocol().serialize_delete_message_request(
            {}, request.message.native_message_id, echo),
        echo);
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
    const auto echo = next_echo();
    const auto response = co_await transport().send_action(
        protocol().serialize_get_chat_member_info_request(
            request.target.native_group_id, request.user_id, request.no_cache,
            echo),
        echo);
    const auto parsed = parse_onebot11_operation_response(response, false);
    if (!parsed.ok()) {
      co_return provider_failure<bot::OneBotGroupMember>(parsed);
    }
    const auto user_id = provider_id(parsed_value(parsed), "user_id");
    if (!user_id) {
      co_return malformed_read<bot::OneBotGroupMember>(
          "OneBot member response is missing user_id");
    }
    co_return bot::BotOperationResult<bot::OneBotGroupMember>::success(
        {.target = request.target,
         .user_id = *user_id,
         .nickname = optional_string(parsed_value(parsed), "nickname"),
         .card = optional_string(parsed_value(parsed), "card"),
         .title = optional_string(parsed_value(parsed), "title")});
  }

  auto execute(const bot::GetOneBotForwardMessageRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::OneBotForwardMessage>> override {
    const auto echo = next_echo();
    const auto response = co_await transport().send_action(
        protocol().serialize_get_forward_msg_request(request.forward_id, echo),
        echo);
    const auto parsed = parse_onebot11_operation_response(response, false);
    if (!parsed.ok()) {
      co_return provider_failure<bot::OneBotForwardMessage>(parsed);
    }
    if (!parsed_value(parsed).is_object() ||
        !parsed_value(parsed).contains("messages") ||
        !parsed_value(parsed).at("messages").is_array()) {
      co_return malformed_read<bot::OneBotForwardMessage>(
          "OneBot forward response is missing messages");
    }
    bot::OneBotForwardMessage result{.installation = installation_,
                                     .forward_id = request.forward_id,
                                     .messages =
                                         parsed_value(parsed).at("messages")};
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
    const auto echo = next_echo();
    const auto response = co_await transport().send_action(
        protocol().serialize_get_group_file_url_request(
            request.target.native_group_id, request.file_id, echo),
        echo);
    const auto parsed = parse_onebot11_operation_response(response, false);
    if (!parsed.ok()) {
      co_return provider_failure<bot::ResolvedOneBotGroupFile>(parsed);
    }
    const auto url = optional_string(parsed_value(parsed), "url");
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
    const auto echo = next_echo();
    const auto response = co_await transport().send_action(
        protocol().serialize_get_private_file_url_request(
            request.user_id, request.file_id, echo),
        echo);
    const auto parsed = parse_onebot11_operation_response(response, false);
    if (!parsed.ok()) {
      co_return provider_failure<bot::ResolvedOneBotPrivateFile>(parsed);
    }
    const auto url = optional_string(parsed_value(parsed), "url");
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
    const auto echo = next_echo();
    const auto response = co_await transport().send_action(
        protocol().serialize_group_poke_request(request.target.native_group_id,
                                                request.user_id, echo),
        echo);
    const auto parsed = parse_onebot11_operation_response(response, true);
    if (!parsed.ok()) {
      co_return provider_failure<bot::OneBotGroupPokeResult>(parsed);
    }
    co_return bot::BotOperationResult<bot::OneBotGroupPokeResult>::success(
        {.target = request.target, .user_id = request.user_id});
  }

private:
  [[nodiscard]] auto next_echo() noexcept -> std::uint64_t {
    return echo_.fetch_add(1, std::memory_order_relaxed);
  }
  [[nodiscard]] auto protocol() const -> adapter::onebot11::ProtocolAdapter & {
    if (protocol_ == nullptr) {
      throw BotComponentRuntimeError("OneBot operations are not prepared");
    }
    return *protocol_;
  }
  [[nodiscard]] auto transport() const -> OneBot11Transport & {
    if (transport_ == nullptr) {
      throw BotComponentRuntimeError("OneBot operations are not prepared");
    }
    return *transport_;
  }

  bot::BotInstallationRef installation_;
  std::shared_ptr<adapter::onebot11::ProtocolAdapter> protocol_;
  std::shared_ptr<OneBot11Transport> transport_;
  std::atomic_uint64_t echo_{};
};

class TelegramMediaUploadComponent::Uploader final
    : public TelegramMediaUploader {
public:
  void configure(std::shared_ptr<TelegramTransport> transport) {
    if (transport == nullptr || transport_ != nullptr) {
      throw BotComponentRuntimeError(
          "Telegram media uploader requires one transport");
    }
    transport_ = std::move(transport);
  }

  auto upload(const bot::SendTelegramMediaGroupUploadsRequest &request)
      -> boost::asio::awaitable<std::string> override {
    if (transport_ == nullptr) {
      throw BotComponentRuntimeError("Telegram media uploader is not prepared");
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
    const auto reply = request.reply_to
                           ? std::optional{request.reply_to->native_message_id}
                           : std::nullopt;
    co_return co_await transport_->upload_media_group(
        request.target.native_group_id, media, request.caption,
        request.topic_id, reply, telegram_entities(request.caption_entities));
  }

private:
  std::shared_ptr<TelegramTransport> transport_;
};

class TelegramOperationsComponent::Endpoint final
    : public BotOperationEndpoint {
public:
  explicit Endpoint(std::string installation_id)
      : installation_{.installation_id = std::move(installation_id),
                      .surface = bot::BotSurface::TelegramBotApi} {
    installation_.validate();
  }

  void configure(std::shared_ptr<adapter::telegram::ProtocolAdapter> protocol,
                 std::shared_ptr<TelegramTransport> transport,
                 std::shared_ptr<TelegramMediaUploader> uploader) {
    if (protocol == nullptr || transport == nullptr || protocol_ != nullptr ||
        transport_ != nullptr) {
      throw BotComponentRuntimeError(
          "Telegram operations require one protocol and transport");
    }
    protocol_ = std::move(protocol);
    transport_ = std::move(transport);
    uploader_ = std::move(uploader);
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
    if (uploader_ != nullptr) {
      actions.push_back(bot::BotAction::SendTelegramMediaGroupUploads);
    }
    return actions;
  }

  auto execute(const bot::SendGroupMessageRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::SendMessageResult>> override {
    const auto echo = next_echo();
    const auto response = co_await transport().send_action(
        protocol().serialize_send_message_request(
            request.target.native_group_id, request.message, echo),
        echo);
    const auto parsed = parse_telegram_operation_response(response, true);
    if (!parsed.ok()) {
      co_return provider_failure<bot::SendMessageResult>(parsed);
    }
    const auto message_id = provider_id(parsed_value(parsed), "message_id");
    if (!message_id) {
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
    const auto echo = next_echo();
    const auto response = co_await transport().send_action(
        protocol().serialize_delete_message_request(
            request.message.group.native_group_id,
            request.message.native_message_id, echo),
        echo);
    const auto parsed = parse_telegram_operation_response(response, true);
    if (!parsed.ok()) {
      co_return provider_failure<bot::DeleteMessageResult>(parsed);
    }
    if (!parsed_value(parsed).is_boolean() ||
        !parsed_value(parsed).get<bool>()) {
      co_return malformed_side_effect<bot::DeleteMessageResult>(
          "Telegram delete response does not confirm deletion");
    }
    co_return bot::BotOperationResult<bot::DeleteMessageResult>::success(
        {.message = request.message});
  }

  auto execute(const bot::SendTelegramTopicMessageRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::SendMessageResult>> override {
    const auto echo = next_echo();
    const auto response = co_await transport().send_action(
        protocol().serialize_send_topic_message_request(
            request.target.group.native_group_id, request.message, echo,
            request.target.topic_id),
        echo);
    co_return telegram_send_result(
        parse_telegram_operation_response(response, true), request.target.group,
        "Telegram topic send", TelegramSendResultShape::SingleMessage);
  }

  auto execute(const bot::EditTelegramMessageTextRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::EditMessageTextResult>> override {
    const auto echo = next_echo();
    const auto response = co_await transport().send_action(
        protocol().serialize_edit_message_text_request(
            request.message.group.native_group_id,
            request.message.native_message_id, request.text, request.parse_mode,
            echo),
        echo);
    const auto parsed = parse_telegram_operation_response(response, true);
    if (!parsed.ok()) {
      co_return provider_failure<bot::EditMessageTextResult>(parsed);
    }
    const auto message_id = provider_id(parsed_value(parsed), "message_id");
    if (!message_id || *message_id != request.message.native_message_id) {
      co_return malformed_side_effect<bot::EditMessageTextResult>(
          "Telegram edit response has no matching message_id");
    }
    co_return bot::BotOperationResult<bot::EditMessageTextResult>::success(
        {.message = request.message});
  }

  auto execute(const bot::SendTelegramPhotoRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::SendMessageResult>> override {
    const auto echo = next_echo();
    nlohmann::json payload{{"method", "sendPhoto"},
                           {"chat_id", request.target.native_group_id},
                           {"photo", request.photo},
                           {"echo", echo}};
    if (!request.caption.empty()) {
      payload["caption"] = request.caption;
    }
    if (!request.caption_entities.empty()) {
      payload["caption_entities"] = request.caption_entities;
    }
    const auto response =
        co_await transport().send_action(payload.dump(), echo);
    co_return telegram_send_result(
        parse_telegram_operation_response(response, true), request.target,
        "Telegram photo send", TelegramSendResultShape::SingleMessage);
  }

  auto execute(const bot::SendTelegramMediaGroupUrlsRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::SendMessageResult>> override {
    const auto echo = next_echo();
    std::vector<std::pair<std::string, std::string>> media;
    media.reserve(request.media.size());
    for (const auto &item : request.media) {
      media.emplace_back(item.type, item.source);
    }
    const auto reply = request.reply_to
                           ? std::optional{request.reply_to->native_message_id}
                           : std::nullopt;
    const auto payload =
        protocol().serialize_send_media_group_request_with_entities(
            request.target.native_group_id, media, request.caption,
            request.topic_id, reply, echo,
            telegram_entities(request.caption_entities));
    const auto response = co_await transport().send_action(payload, echo);
    co_return telegram_send_result(
        parse_telegram_operation_response(response, true), request.target,
        "Telegram URL media-group send", TelegramSendResultShape::MediaGroup,
        request.media.size());
  }

  auto execute(const bot::SendTelegramMediaGroupUploadsRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::SendMessageResult>> override {
    if (uploader_ == nullptr) {
      co_return bot::failed_operation<bot::SendMessageResult>(
          bot::BotOperationErrorCode::UnsupportedAction,
          "Telegram multipart media-group upload is unavailable");
    }
    const auto response = // NOLINT(clang-analyzer-core.CallAndMessage)
        co_await uploader_->upload(request);
    co_return telegram_send_result(
        parse_telegram_operation_response(response, true), request.target,
        "Telegram multipart media-group send",
        TelegramSendResultShape::MediaGroup, request.media.size());
  }

  auto execute(const bot::FetchTelegramFileRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::FetchedTelegramFile>> override {
    const auto download_url =
        co_await transport().download_file(request.file.file_id);
    if (download_url.empty()) {
      co_return bot::failed_operation<bot::FetchedTelegramFile>(
          bot::BotOperationErrorCode::ProviderRejected,
          "Telegram did not resolve the requested file");
    }
    const auto content = co_await transport().download_file_content(
        download_url, request.maximum_bytes);
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
  [[nodiscard]] auto next_echo() noexcept -> std::uint64_t {
    return echo_.fetch_add(1, std::memory_order_relaxed);
  }
  [[nodiscard]] auto protocol() const -> adapter::telegram::ProtocolAdapter & {
    if (protocol_ == nullptr) {
      throw BotComponentRuntimeError("Telegram operations are not prepared");
    }
    return *protocol_;
  }
  [[nodiscard]] auto transport() const -> TelegramTransport & {
    if (transport_ == nullptr) {
      throw BotComponentRuntimeError("Telegram operations are not prepared");
    }
    return *transport_;
  }

  bot::BotInstallationRef installation_;
  std::shared_ptr<adapter::telegram::ProtocolAdapter> protocol_;
  std::shared_ptr<TelegramTransport> transport_;
  std::shared_ptr<TelegramMediaUploader> uploader_;
  std::atomic_uint64_t echo_{};
};

TelegramMediaUploadComponent::TelegramMediaUploadComponent()
    : uploader_(std::make_shared<Uploader>()) {}

auto TelegramMediaUploadComponent::descriptor() const -> ComponentDescriptor {
  return {
      .id = ComponentId{"telegram.media-upload"},
      .provides = {CapabilityId{
          std::string{bot_capability_ids::telegram_media_upload}}},
      .required = {CapabilityId{
          std::string{bot_capability_ids::telegram_transport}}},
  };
}

void TelegramMediaUploadComponent::install_capabilities(
    CapabilityRegistry &registry) {
  registry.install<TelegramMediaUploader>(
      ComponentId{"telegram.media-upload"},
      CapabilityId{std::string{bot_capability_ids::telegram_media_upload}},
      uploader_);
}

void TelegramMediaUploadComponent::prepare(const CapabilityRegistry &registry) {
  uploader_->configure(registry.get<TelegramTransport>(
      CapabilityId{std::string{bot_capability_ids::telegram_transport}}));
}

void TelegramMediaUploadComponent::start() {}
void TelegramMediaUploadComponent::stop() {}

OneBot11OperationsComponent::OneBot11OperationsComponent(
    std::string installation_id)
    : endpoint_(std::make_shared<Endpoint>(std::move(installation_id))) {}

auto OneBot11OperationsComponent::descriptor() const -> ComponentDescriptor {
  return {
      .id = ComponentId{"onebot11.operations"},
      .provides = {CapabilityId{std::string{bot_capability_ids::operations}}},
      .required =
          {CapabilityId{std::string{bot_capability_ids::onebot11_protocol}},
           CapabilityId{std::string{bot_capability_ids::onebot11_transport}}},
  };
}

void OneBot11OperationsComponent::install_capabilities(
    CapabilityRegistry &registry) {
  registry.install<BotOperationEndpoint>(
      ComponentId{"onebot11.operations"},
      CapabilityId{std::string{bot_capability_ids::operations}}, endpoint_);
}

void OneBot11OperationsComponent::prepare(const CapabilityRegistry &registry) {
  endpoint_->configure(
      registry.get<adapter::onebot11::ProtocolAdapter>(
          CapabilityId{std::string{bot_capability_ids::onebot11_protocol}}),
      registry.get<OneBot11Transport>(
          CapabilityId{std::string{bot_capability_ids::onebot11_transport}}));
}

void OneBot11OperationsComponent::start() {}
void OneBot11OperationsComponent::stop() {}

TelegramOperationsComponent::TelegramOperationsComponent(
    std::string installation_id)
    : endpoint_(std::make_shared<Endpoint>(std::move(installation_id))) {}

auto TelegramOperationsComponent::descriptor() const -> ComponentDescriptor {
  return {
      .id = ComponentId{"telegram.operations"},
      .provides = {CapabilityId{std::string{bot_capability_ids::operations}}},
      .required =
          {CapabilityId{std::string{bot_capability_ids::telegram_protocol}},
           CapabilityId{std::string{bot_capability_ids::telegram_transport}}},
  };
}

void TelegramOperationsComponent::install_capabilities(
    CapabilityRegistry &registry) {
  registry.install<BotOperationEndpoint>(
      ComponentId{"telegram.operations"},
      CapabilityId{std::string{bot_capability_ids::operations}}, endpoint_);
}

void TelegramOperationsComponent::prepare(const CapabilityRegistry &registry) {
  std::shared_ptr<TelegramMediaUploader> uploader;
  const CapabilityId uploader_id{
      std::string{bot_capability_ids::telegram_media_upload}};
  if (registry.contains(uploader_id)) {
    uploader = registry.get<TelegramMediaUploader>(uploader_id);
  }
  endpoint_->configure(
      registry.get<adapter::telegram::ProtocolAdapter>(
          CapabilityId{std::string{bot_capability_ids::telegram_protocol}}),
      registry.get<TelegramTransport>(
          CapabilityId{std::string{bot_capability_ids::telegram_transport}}),
      std::move(uploader));
}

void TelegramOperationsComponent::start() {}
void TelegramOperationsComponent::stop() {}

} // namespace obcx::core
