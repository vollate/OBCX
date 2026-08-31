#ifndef OBCX_INCLUDE_CORE_ONEBOT11_BOT_OPERATIONS_HPP_
#define OBCX_INCLUDE_CORE_ONEBOT11_BOT_OPERATIONS_HPP_

#include "core/bot/bot_operations.hpp"

#include <optional>
#include <stdexcept>
#include <string>

namespace obcx::bot {

namespace detail {

inline void require_onebot(const BotInstallationRef &installation,
                           const std::string_view type) {
  installation.validate();
  if (installation.surface != BotSurface::OneBot11Qq) {
    throw std::invalid_argument(std::string{type} + " requires onebot11.qq");
  }
}

} // namespace detail

struct GetOneBotGroupMemberRequest {
  static constexpr BotAction action = BotAction::GetOneBotGroupMember;

  GroupTarget target;
  std::string user_id;
  bool no_cache{};

  void validate() const {
    target.validate();
    detail::require_onebot(target.installation, "GetOneBotGroupMemberRequest");
    detail::validate_identifier(user_id, "OneBot user_id");
  }
};

struct OneBotGroupMember {
  GroupTarget target;
  std::string user_id;
  std::string nickname;
  std::string card;
  std::string title;

  void validate() const {
    target.validate();
    detail::require_onebot(target.installation, "OneBotGroupMember");
    detail::validate_identifier(user_id, "OneBot user_id");
    if (!nickname.empty()) {
      detail::validate_identifier(nickname, "OneBot nickname", 1024);
    }
    if (!card.empty()) {
      detail::validate_identifier(card, "OneBot card", 1024);
    }
    if (!title.empty()) {
      detail::validate_identifier(title, "OneBot title", 1024);
    }
  }

  auto operator==(const OneBotGroupMember &) const -> bool = default;
};

struct GetOneBotForwardMessageRequest {
  static constexpr BotAction action = BotAction::GetOneBotForwardMessage;

  BotInstallationRef installation;
  std::string forward_id;

  void validate() const {
    detail::require_onebot(installation, "GetOneBotForwardMessageRequest");
    detail::validate_identifier(forward_id, "OneBot forward_id");
  }
};

struct OneBotForwardMessage {
  BotInstallationRef installation;
  std::string forward_id;
  Json messages = Json::array();

  void validate() const {
    detail::require_onebot(installation, "OneBotForwardMessage");
    detail::validate_identifier(forward_id, "OneBot forward_id");
    if (!messages.is_array()) {
      throw std::invalid_argument("OneBot forwarded messages must be an array");
    }
    for (const auto &message : messages) {
      if (!message.is_object()) {
        throw std::invalid_argument(
            "OneBot forwarded message nodes must be objects");
      }
    }
  }

  auto operator==(const OneBotForwardMessage &) const -> bool = default;
};

struct ResolveOneBotGroupFileRequest {
  static constexpr BotAction action = BotAction::ResolveOneBotGroupFile;

  GroupTarget target;
  std::string file_id;

  void validate() const {
    target.validate();
    detail::require_onebot(target.installation,
                           "ResolveOneBotGroupFileRequest");
    detail::validate_identifier(file_id, "OneBot file_id", 2048);
  }
};

struct ResolveOneBotPrivateFileRequest {
  static constexpr BotAction action = BotAction::ResolveOneBotPrivateFile;

  BotInstallationRef installation;
  std::string user_id;
  std::string file_id;

  void validate() const {
    detail::require_onebot(installation, "ResolveOneBotPrivateFileRequest");
    detail::validate_identifier(user_id, "OneBot user_id");
    detail::validate_identifier(file_id, "OneBot file_id", 2048);
  }
};

struct ResolvedOneBotGroupFile {
  GroupTarget target;
  std::string file_id;
  std::string url;

  void validate() const {
    target.validate();
    detail::require_onebot(target.installation, "ResolvedOneBotGroupFile");
    detail::validate_identifier(file_id, "OneBot file_id", 2048);
    detail::validate_identifier(url, "OneBot file URL", 16384);
  }

  auto operator==(const ResolvedOneBotGroupFile &) const -> bool = default;
};

struct ResolvedOneBotPrivateFile {
  BotInstallationRef installation;
  std::string user_id;
  std::string file_id;
  std::string url;

  void validate() const {
    detail::require_onebot(installation, "ResolvedOneBotPrivateFile");
    detail::validate_identifier(user_id, "OneBot user_id");
    detail::validate_identifier(file_id, "OneBot file_id", 2048);
    detail::validate_identifier(url, "OneBot file URL", 16384);
  }

  auto operator==(const ResolvedOneBotPrivateFile &) const -> bool = default;
};

struct PokeOneBotGroupRequest {
  static constexpr BotAction action = BotAction::PokeOneBotGroup;

  GroupTarget target;
  std::string user_id;

  void validate() const {
    target.validate();
    detail::require_onebot(target.installation, "PokeOneBotGroupRequest");
    detail::validate_identifier(user_id, "OneBot user_id");
  }
};

struct OneBotGroupPokeResult {
  GroupTarget target;
  std::string user_id;

  void validate() const {
    target.validate();
    detail::require_onebot(target.installation, "OneBotGroupPokeResult");
    detail::validate_identifier(user_id, "OneBot user_id");
  }

  auto operator==(const OneBotGroupPokeResult &) const -> bool = default;
};

inline void to_json(Json &document,
                    const GetOneBotGroupMemberRequest &request) {
  request.validate();
  document = {{"action", request.action},
              {"target", request.target},
              {"user_id", request.user_id},
              {"no_cache", request.no_cache}};
}

inline void from_json(const Json &document,
                      GetOneBotGroupMemberRequest &request) {
  detail::require_object(document, "GetOneBotGroupMemberRequest");
  if (document.contains("action") &&
      document.at("action").get<BotAction>() != request.action) {
    throw std::invalid_argument("GetOneBotGroupMemberRequest action mismatch");
  }
  if (!document.contains("target")) {
    throw std::invalid_argument("GetOneBotGroupMemberRequest requires target");
  }
  request.target = document.at("target").get<GroupTarget>();
  request.user_id = detail::require_string(document, "user_id",
                                           "GetOneBotGroupMemberRequest");
  request.no_cache = false;
  if (document.contains("no_cache")) {
    if (!document.at("no_cache").is_boolean()) {
      throw std::invalid_argument("no_cache must be boolean");
    }
    request.no_cache = document.at("no_cache").get<bool>();
  }
  request.validate();
}

inline void to_json(Json &document, const OneBotGroupMember &member) {
  member.validate();
  document = {{"target", member.target},
              {"user_id", member.user_id},
              {"nickname", member.nickname},
              {"card", member.card},
              {"title", member.title}};
}

inline void from_json(const Json &document, OneBotGroupMember &member) {
  detail::require_object(document, "OneBotGroupMember");
  if (!document.contains("target")) {
    throw std::invalid_argument("OneBotGroupMember requires target");
  }
  member.target = document.at("target").get<GroupTarget>();
  member.user_id =
      detail::require_string(document, "user_id", "OneBotGroupMember");
  member.nickname = document.value("nickname", std::string{});
  member.card = document.value("card", std::string{});
  member.title = document.value("title", std::string{});
  member.validate();
}

inline void to_json(Json &document,
                    const GetOneBotForwardMessageRequest &request) {
  request.validate();
  document = {{"action", request.action},
              {"installation", request.installation},
              {"forward_id", request.forward_id}};
}

inline void from_json(const Json &document,
                      GetOneBotForwardMessageRequest &request) {
  detail::require_object(document, "GetOneBotForwardMessageRequest");
  if (document.contains("action") &&
      document.at("action").get<BotAction>() != request.action) {
    throw std::invalid_argument(
        "GetOneBotForwardMessageRequest action mismatch");
  }
  if (!document.contains("installation")) {
    throw std::invalid_argument(
        "GetOneBotForwardMessageRequest requires installation");
  }
  request.installation = document.at("installation").get<BotInstallationRef>();
  request.forward_id = detail::require_string(document, "forward_id",
                                              "GetOneBotForwardMessageRequest");
  request.validate();
}

inline void to_json(Json &document, const OneBotForwardMessage &message) {
  message.validate();
  document = {{"installation", message.installation},
              {"forward_id", message.forward_id},
              {"messages", message.messages}};
}

inline void from_json(const Json &document, OneBotForwardMessage &message) {
  detail::require_object(document, "OneBotForwardMessage");
  if (!document.contains("installation") || !document.contains("messages")) {
    throw std::invalid_argument(
        "OneBotForwardMessage requires installation and messages");
  }
  message.installation = document.at("installation").get<BotInstallationRef>();
  message.forward_id =
      detail::require_string(document, "forward_id", "OneBotForwardMessage");
  message.messages = document.at("messages");
  message.validate();
}

inline void to_json(Json &document,
                    const ResolveOneBotGroupFileRequest &request) {
  request.validate();
  document = {{"action", request.action},
              {"target", request.target},
              {"file_id", request.file_id}};
}

inline void from_json(const Json &document,
                      ResolveOneBotGroupFileRequest &request) {
  detail::require_object(document, "ResolveOneBotGroupFileRequest");
  if (document.contains("action") &&
      document.at("action").get<BotAction>() != request.action) {
    throw std::invalid_argument(
        "ResolveOneBotGroupFileRequest action mismatch");
  }
  if (!document.contains("target")) {
    throw std::invalid_argument(
        "ResolveOneBotGroupFileRequest requires target");
  }
  request.target = document.at("target").get<GroupTarget>();
  request.file_id = detail::require_string(document, "file_id",
                                           "ResolveOneBotGroupFileRequest");
  request.validate();
}

inline void to_json(Json &document,
                    const ResolveOneBotPrivateFileRequest &request) {
  request.validate();
  document = {{"action", request.action},
              {"installation", request.installation},
              {"user_id", request.user_id},
              {"file_id", request.file_id}};
}

inline void from_json(const Json &document,
                      ResolveOneBotPrivateFileRequest &request) {
  detail::require_object(document, "ResolveOneBotPrivateFileRequest");
  if (document.contains("action") &&
      document.at("action").get<BotAction>() != request.action) {
    throw std::invalid_argument(
        "ResolveOneBotPrivateFileRequest action mismatch");
  }
  if (!document.contains("installation")) {
    throw std::invalid_argument(
        "ResolveOneBotPrivateFileRequest requires installation");
  }
  request.installation = document.at("installation").get<BotInstallationRef>();
  request.user_id = detail::require_string(document, "user_id",
                                           "ResolveOneBotPrivateFileRequest");
  request.file_id = detail::require_string(document, "file_id",
                                           "ResolveOneBotPrivateFileRequest");
  request.validate();
}

inline void to_json(Json &document, const ResolvedOneBotGroupFile &file) {
  file.validate();
  document = {
      {"target", file.target}, {"file_id", file.file_id}, {"url", file.url}};
}

inline void from_json(const Json &document, ResolvedOneBotGroupFile &file) {
  detail::require_object(document, "ResolvedOneBotGroupFile");
  if (!document.contains("target")) {
    throw std::invalid_argument("ResolvedOneBotGroupFile requires target");
  }
  file.target = document.at("target").get<GroupTarget>();
  file.file_id =
      detail::require_string(document, "file_id", "ResolvedOneBotGroupFile");
  file.url = detail::require_string(document, "url", "ResolvedOneBotGroupFile");
  file.validate();
}

inline void to_json(Json &document, const ResolvedOneBotPrivateFile &file) {
  file.validate();
  document = {{"installation", file.installation},
              {"user_id", file.user_id},
              {"file_id", file.file_id},
              {"url", file.url}};
}

inline void from_json(const Json &document, ResolvedOneBotPrivateFile &file) {
  detail::require_object(document, "ResolvedOneBotPrivateFile");
  if (!document.contains("installation")) {
    throw std::invalid_argument(
        "ResolvedOneBotPrivateFile requires installation");
  }
  file.installation = document.at("installation").get<BotInstallationRef>();
  file.user_id =
      detail::require_string(document, "user_id", "ResolvedOneBotPrivateFile");
  file.file_id =
      detail::require_string(document, "file_id", "ResolvedOneBotPrivateFile");
  file.url =
      detail::require_string(document, "url", "ResolvedOneBotPrivateFile");
  file.validate();
}

inline void to_json(Json &document, const PokeOneBotGroupRequest &request) {
  request.validate();
  document = {{"action", request.action},
              {"target", request.target},
              {"user_id", request.user_id}};
}

inline void from_json(const Json &document, PokeOneBotGroupRequest &request) {
  detail::require_object(document, "PokeOneBotGroupRequest");
  if (document.contains("action") &&
      document.at("action").get<BotAction>() != request.action) {
    throw std::invalid_argument("PokeOneBotGroupRequest action mismatch");
  }
  if (!document.contains("target")) {
    throw std::invalid_argument("PokeOneBotGroupRequest requires target");
  }
  request.target = document.at("target").get<GroupTarget>();
  request.user_id =
      detail::require_string(document, "user_id", "PokeOneBotGroupRequest");
  request.validate();
}

inline void to_json(Json &document, const OneBotGroupPokeResult &result) {
  result.validate();
  document = {{"target", result.target}, {"user_id", result.user_id}};
}

inline void from_json(const Json &document, OneBotGroupPokeResult &result) {
  detail::require_object(document, "OneBotGroupPokeResult");
  if (!document.contains("target")) {
    throw std::invalid_argument("OneBotGroupPokeResult requires target");
  }
  result.target = document.at("target").get<GroupTarget>();
  result.user_id =
      detail::require_string(document, "user_id", "OneBotGroupPokeResult");
  result.validate();
}

} // namespace obcx::bot

#endif // OBCX_INCLUDE_CORE_ONEBOT11_BOT_OPERATIONS_HPP_
