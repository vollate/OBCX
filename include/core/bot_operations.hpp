#pragma once

#include "common/message_type.hpp"
#include "core/bot_operation_types.hpp"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace obcx::bot {

namespace detail {

inline void validate_message(const common::Message &message,
                             const std::string_view field = "message") {
  if (message.empty()) {
    throw std::invalid_argument(std::string{field} + " cannot be empty");
  }
  for (const auto &segment : message) {
    validate_identifier(segment.type, "message segment type", 128);
    if (!segment.data.is_object()) {
      throw std::invalid_argument("message segment data must be an object");
    }
  }
}

inline auto require_message(const Json &document, const std::string_view key,
                            const std::string_view type) -> common::Message {
  const auto field = std::string{key};
  if (!document.contains(field) || !document.at(field).is_array()) {
    throw std::invalid_argument(std::string{type} + " requires array " + field);
  }
  auto message = document.at(field).get<common::Message>();
  validate_message(message, key);
  return message;
}

} // namespace detail

struct SendGroupMessageRequest {
  static constexpr BotAction action = BotAction::SendGroupMessage;

  GroupTarget target;
  common::Message message;

  void validate() const {
    target.validate();
    detail::validate_message(message);
  }
};

struct SendTelegramTopicMessageRequest {
  static constexpr BotAction action = BotAction::SendTelegramTopicMessage;

  TelegramTopicTarget target;
  common::Message message;

  void validate() const {
    target.validate();
    detail::validate_message(message);
  }
};

struct DeleteMessageRequest {
  static constexpr BotAction action = BotAction::DeleteMessage;

  BotMessageRef message;

  void validate() const { message.validate(); }
};

struct EditTelegramMessageTextRequest {
  static constexpr BotAction action = BotAction::EditTelegramMessageText;

  BotMessageRef message;
  std::string text;
  std::string parse_mode;

  void validate() const {
    message.validate();
    if (message.group.installation.surface != BotSurface::TelegramBotApi) {
      throw std::invalid_argument(
          "EditTelegramMessageTextRequest requires telegram.bot_api");
    }
    detail::validate_identifier(text, "edit text", 65536);
    if (!parse_mode.empty()) {
      detail::validate_identifier(parse_mode, "parse_mode", 64);
    }
  }
};

struct SendMessageResult {
  std::vector<BotMessageRef> messages;

  void validate() const {
    if (messages.empty()) {
      throw std::invalid_argument(
          "SendMessageResult requires at least one message");
    }
    for (const auto &message : messages) {
      message.validate();
      if (message.group != messages.front().group) {
        throw std::invalid_argument(
            "SendMessageResult messages must share one target group");
      }
    }
  }

  [[nodiscard]] auto primary() const -> const BotMessageRef & {
    validate();
    return messages.front();
  }

  auto operator==(const SendMessageResult &) const -> bool = default;
};

struct DeleteMessageResult {
  BotMessageRef message;

  void validate() const { message.validate(); }

  auto operator==(const DeleteMessageResult &) const -> bool = default;
};

struct EditMessageTextResult {
  BotMessageRef message;

  void validate() const {
    message.validate();
    if (message.group.installation.surface != BotSurface::TelegramBotApi) {
      throw std::invalid_argument(
          "EditMessageTextResult requires telegram.bot_api");
    }
  }

  auto operator==(const EditMessageTextResult &) const -> bool = default;
};

inline void to_json(Json &document, const SendGroupMessageRequest &request) {
  request.validate();
  document = {{"action", request.action},
              {"target", request.target},
              {"message", request.message}};
}

inline void from_json(const Json &document, SendGroupMessageRequest &request) {
  detail::require_object(document, "SendGroupMessageRequest");
  if (document.contains("action") &&
      document.at("action").get<BotAction>() != request.action) {
    throw std::invalid_argument("SendGroupMessageRequest action mismatch");
  }
  if (!document.contains("target")) {
    throw std::invalid_argument("SendGroupMessageRequest requires target");
  }
  request.target = document.at("target").get<GroupTarget>();
  request.message =
      detail::require_message(document, "message", "SendGroupMessageRequest");
  request.validate();
}

inline void to_json(Json &document,
                    const SendTelegramTopicMessageRequest &request) {
  request.validate();
  document = {{"action", request.action},
              {"target", request.target},
              {"message", request.message}};
}

inline void from_json(const Json &document,
                      SendTelegramTopicMessageRequest &request) {
  detail::require_object(document, "SendTelegramTopicMessageRequest");
  if (document.contains("action") &&
      document.at("action").get<BotAction>() != request.action) {
    throw std::invalid_argument(
        "SendTelegramTopicMessageRequest action mismatch");
  }
  if (!document.contains("target")) {
    throw std::invalid_argument(
        "SendTelegramTopicMessageRequest requires target");
  }
  request.target = document.at("target").get<TelegramTopicTarget>();
  request.message = detail::require_message(document, "message",
                                            "SendTelegramTopicMessageRequest");
  request.validate();
}

inline void to_json(Json &document, const DeleteMessageRequest &request) {
  request.validate();
  document = {{"action", request.action}, {"message", request.message}};
}

inline void from_json(const Json &document, DeleteMessageRequest &request) {
  detail::require_object(document, "DeleteMessageRequest");
  if (document.contains("action") &&
      document.at("action").get<BotAction>() != request.action) {
    throw std::invalid_argument("DeleteMessageRequest action mismatch");
  }
  if (!document.contains("message")) {
    throw std::invalid_argument("DeleteMessageRequest requires message");
  }
  request.message = document.at("message").get<BotMessageRef>();
  request.validate();
}

inline void to_json(Json &document,
                    const EditTelegramMessageTextRequest &request) {
  request.validate();
  document = {{"action", request.action},
              {"message", request.message},
              {"text", request.text},
              {"parse_mode", request.parse_mode}};
}

inline void from_json(const Json &document,
                      EditTelegramMessageTextRequest &request) {
  detail::require_object(document, "EditTelegramMessageTextRequest");
  if (document.contains("action") &&
      document.at("action").get<BotAction>() != request.action) {
    throw std::invalid_argument(
        "EditTelegramMessageTextRequest action mismatch");
  }
  if (!document.contains("message")) {
    throw std::invalid_argument(
        "EditTelegramMessageTextRequest requires message");
  }
  request.message = document.at("message").get<BotMessageRef>();
  request.text = detail::require_string(document, "text",
                                        "EditTelegramMessageTextRequest");
  request.parse_mode.clear();
  if (document.contains("parse_mode")) {
    request.parse_mode = detail::require_string(
        document, "parse_mode", "EditTelegramMessageTextRequest");
  }
  request.validate();
}

inline void to_json(Json &document, const SendMessageResult &result) {
  result.validate();
  document = {{"messages", result.messages}};
}

inline void from_json(const Json &document, SendMessageResult &result) {
  detail::require_object(document, "SendMessageResult");
  if (!document.contains("messages") || !document.at("messages").is_array()) {
    throw std::invalid_argument("SendMessageResult requires messages array");
  }
  result.messages = document.at("messages").get<std::vector<BotMessageRef>>();
  result.validate();
}

inline void to_json(Json &document, const DeleteMessageResult &result) {
  result.validate();
  document = {{"message", result.message}};
}

inline void from_json(const Json &document, DeleteMessageResult &result) {
  detail::require_object(document, "DeleteMessageResult");
  if (!document.contains("message")) {
    throw std::invalid_argument("DeleteMessageResult requires message");
  }
  result.message = document.at("message").get<BotMessageRef>();
  result.validate();
}

inline void to_json(Json &document, const EditMessageTextResult &result) {
  result.validate();
  document = {{"message", result.message}};
}

inline void from_json(const Json &document, EditMessageTextResult &result) {
  detail::require_object(document, "EditMessageTextResult");
  if (!document.contains("message")) {
    throw std::invalid_argument("EditMessageTextResult requires message");
  }
  result.message = document.at("message").get<BotMessageRef>();
  result.validate();
}

} // namespace obcx::bot
