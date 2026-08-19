#pragma once

#include "common/json_utils.hpp"

#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace obcx::bot {

using Json = common::json;

enum class BotSurface : std::uint8_t { TelegramBotApi, OneBot11Qq };

enum class BotAction : std::uint8_t {
  SendGroupMessage,
  DeleteMessage,
  SendTelegramTopicMessage,
  EditTelegramMessageText,
  SendTelegramPhoto,
  SendTelegramMediaGroupUrls,
  SendTelegramMediaGroupUploads,
  FetchTelegramFile,
  GetOneBotGroupMember,
  GetOneBotForwardMessage,
  ResolveOneBotGroupFile,
  ResolveOneBotPrivateFile,
  PokeOneBotGroup,
};

namespace action_ids {
inline constexpr std::string_view send_group_message = "message.send_group";
inline constexpr std::string_view delete_message = "message.delete";
inline constexpr std::string_view send_telegram_topic_message =
    "telegram.message.send_topic";
inline constexpr std::string_view edit_telegram_message_text =
    "telegram.message.edit_text";
inline constexpr std::string_view send_telegram_photo =
    "telegram.media.send_photo";
inline constexpr std::string_view send_telegram_media_group_urls =
    "telegram.media.send_group_urls";
inline constexpr std::string_view send_telegram_media_group_uploads =
    "telegram.media.send_group_uploads";
inline constexpr std::string_view fetch_telegram_file =
    "telegram.media.fetch_file";
inline constexpr std::string_view get_onebot_group_member =
    "onebot11.group_member.get";
inline constexpr std::string_view get_onebot_forward_message =
    "onebot11.forward_message.get";
inline constexpr std::string_view resolve_onebot_group_file =
    "onebot11.group_file.resolve";
inline constexpr std::string_view resolve_onebot_private_file =
    "onebot11.private_file.resolve";
inline constexpr std::string_view poke_onebot_group = "onebot11.group.poke";

inline constexpr std::array<std::string_view, 13> all = {
    send_group_message,
    delete_message,
    send_telegram_topic_message,
    edit_telegram_message_text,
    send_telegram_photo,
    send_telegram_media_group_urls,
    send_telegram_media_group_uploads,
    fetch_telegram_file,
    get_onebot_group_member,
    get_onebot_forward_message,
    resolve_onebot_group_file,
    resolve_onebot_private_file,
    poke_onebot_group,
};
} // namespace action_ids

enum class SubmissionSafety : std::uint8_t {
  DefinitelyNotSubmitted,
  PossiblySubmitted,
};

enum class BotOperationErrorCode : std::uint8_t {
  InvalidRequest,
  RouteNotFound,
  SurfaceMismatch,
  UnsupportedAction,
  ProviderRejected,
  MalformedResponse,
  TransportFailure,
  MediaTooLarge,
  Cancelled,
  OutcomeUnknown,
};

[[nodiscard]] inline auto redact_bot_diagnostic(const std::string_view value)
    -> std::string {
  std::string lowered;
  lowered.reserve(value.size());
  for (const auto character : value) {
    lowered.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
  }
  constexpr std::array sensitive_markers = {
      std::string_view{"authorization:"},
      std::string_view{"bearer "},
      std::string_view{"access_token"},
      std::string_view{"api_key="},
      std::string_view{"apikey="},
      std::string_view{"secret="},
      std::string_view{"token="},
      std::string_view{"api.telegram.org/file/bot"},
  };
  for (const auto marker : sensitive_markers) {
    if (lowered.contains(marker)) {
      return "[redacted provider diagnostic]";
    }
  }
  return std::string{value};
}

namespace detail {

inline void require_object(const Json &document, const std::string_view type) {
  if (!document.is_object()) {
    throw std::invalid_argument(std::string{type} + " must be an object");
  }
}

inline auto require_string(const Json &document, const std::string_view key,
                           const std::string_view type) -> std::string {
  const auto field = std::string{key};
  if (!document.contains(field) || !document.at(field).is_string()) {
    throw std::invalid_argument(std::string{type} + " requires string " +
                                field);
  }
  return document.at(field).get<std::string>();
}

inline void validate_identifier(const std::string_view value,
                                const std::string_view field,
                                const std::size_t maximum = 1024) {
  if (value.empty()) {
    throw std::invalid_argument(std::string{field} + " cannot be empty");
  }
  if (value.size() > maximum) {
    throw std::invalid_argument(std::string{field} + " exceeds its limit");
  }
  for (const auto character : value) {
    const auto byte = static_cast<unsigned char>(character);
    if (byte < 0x20U || byte == 0x7FU) {
      throw std::invalid_argument(std::string{field} +
                                  " contains a control character");
    }
  }
}

} // namespace detail

[[nodiscard]] constexpr auto surface_id(const BotSurface surface)
    -> std::string_view {
  switch (surface) {
  case BotSurface::TelegramBotApi:
    return "telegram.bot_api";
  case BotSurface::OneBot11Qq:
    return "onebot11.qq";
  }
  return {};
}

[[nodiscard]] inline auto bot_surface_from_id(const std::string_view value)
    -> BotSurface {
  if (value == "telegram.bot_api") {
    return BotSurface::TelegramBotApi;
  }
  if (value == "onebot11.qq") {
    return BotSurface::OneBot11Qq;
  }
  throw std::invalid_argument("unsupported bot surface: " + std::string{value});
}

[[nodiscard]] constexpr auto action_id(const BotAction action)
    -> std::string_view {
  switch (action) {
  case BotAction::SendGroupMessage:
    return action_ids::send_group_message;
  case BotAction::DeleteMessage:
    return action_ids::delete_message;
  case BotAction::SendTelegramTopicMessage:
    return action_ids::send_telegram_topic_message;
  case BotAction::EditTelegramMessageText:
    return action_ids::edit_telegram_message_text;
  case BotAction::SendTelegramPhoto:
    return action_ids::send_telegram_photo;
  case BotAction::SendTelegramMediaGroupUrls:
    return action_ids::send_telegram_media_group_urls;
  case BotAction::SendTelegramMediaGroupUploads:
    return action_ids::send_telegram_media_group_uploads;
  case BotAction::FetchTelegramFile:
    return action_ids::fetch_telegram_file;
  case BotAction::GetOneBotGroupMember:
    return action_ids::get_onebot_group_member;
  case BotAction::GetOneBotForwardMessage:
    return action_ids::get_onebot_forward_message;
  case BotAction::ResolveOneBotGroupFile:
    return action_ids::resolve_onebot_group_file;
  case BotAction::ResolveOneBotPrivateFile:
    return action_ids::resolve_onebot_private_file;
  case BotAction::PokeOneBotGroup:
    return action_ids::poke_onebot_group;
  }
  return {};
}

[[nodiscard]] inline auto bot_action_from_id(const std::string_view value)
    -> BotAction {
  for (std::size_t index = 0; index < action_ids::all.size(); ++index) {
    if (action_ids::all[index] == value) {
      return static_cast<BotAction>(index);
    }
  }
  throw std::invalid_argument("unsupported bot action: " + std::string{value});
}

[[nodiscard]] constexpr auto action_supports_surface(const BotAction action,
                                                     const BotSurface surface)
    -> bool {
  switch (action) {
  case BotAction::SendGroupMessage:
  case BotAction::DeleteMessage:
    return true;
  case BotAction::SendTelegramTopicMessage:
  case BotAction::EditTelegramMessageText:
  case BotAction::SendTelegramPhoto:
  case BotAction::SendTelegramMediaGroupUrls:
  case BotAction::SendTelegramMediaGroupUploads:
  case BotAction::FetchTelegramFile:
    return surface == BotSurface::TelegramBotApi;
  case BotAction::GetOneBotGroupMember:
  case BotAction::GetOneBotForwardMessage:
  case BotAction::ResolveOneBotGroupFile:
  case BotAction::ResolveOneBotPrivateFile:
  case BotAction::PokeOneBotGroup:
    return surface == BotSurface::OneBot11Qq;
  }
  return false;
}

[[nodiscard]] constexpr auto submission_safety_id(const SubmissionSafety safety)
    -> std::string_view {
  switch (safety) {
  case SubmissionSafety::DefinitelyNotSubmitted:
    return "definitely_not_submitted";
  case SubmissionSafety::PossiblySubmitted:
    return "possibly_submitted";
  }
  return {};
}

[[nodiscard]] inline auto submission_safety_from_id(
    const std::string_view value) -> SubmissionSafety {
  if (value == "definitely_not_submitted") {
    return SubmissionSafety::DefinitelyNotSubmitted;
  }
  if (value == "possibly_submitted") {
    return SubmissionSafety::PossiblySubmitted;
  }
  throw std::invalid_argument("unsupported submission safety: " +
                              std::string{value});
}

[[nodiscard]] constexpr auto error_code_id(const BotOperationErrorCode code)
    -> std::string_view {
  switch (code) {
  case BotOperationErrorCode::InvalidRequest:
    return "invalid_request";
  case BotOperationErrorCode::RouteNotFound:
    return "route_not_found";
  case BotOperationErrorCode::SurfaceMismatch:
    return "surface_mismatch";
  case BotOperationErrorCode::UnsupportedAction:
    return "unsupported_action";
  case BotOperationErrorCode::ProviderRejected:
    return "provider_rejected";
  case BotOperationErrorCode::MalformedResponse:
    return "malformed_response";
  case BotOperationErrorCode::TransportFailure:
    return "transport_failure";
  case BotOperationErrorCode::MediaTooLarge:
    return "media_too_large";
  case BotOperationErrorCode::Cancelled:
    return "cancelled";
  case BotOperationErrorCode::OutcomeUnknown:
    return "outcome_unknown";
  }
  return {};
}

[[nodiscard]] inline auto error_code_from_id(const std::string_view value)
    -> BotOperationErrorCode {
  constexpr std::array codes = {
      BotOperationErrorCode::InvalidRequest,
      BotOperationErrorCode::RouteNotFound,
      BotOperationErrorCode::SurfaceMismatch,
      BotOperationErrorCode::UnsupportedAction,
      BotOperationErrorCode::ProviderRejected,
      BotOperationErrorCode::MalformedResponse,
      BotOperationErrorCode::TransportFailure,
      BotOperationErrorCode::MediaTooLarge,
      BotOperationErrorCode::Cancelled,
      BotOperationErrorCode::OutcomeUnknown,
  };
  for (const auto code : codes) {
    if (error_code_id(code) == value) {
      return code;
    }
  }
  throw std::invalid_argument("unsupported bot operation error code: " +
                              std::string{value});
}

struct BotInstallationRef {
  std::string installation_id;
  BotSurface surface{BotSurface::TelegramBotApi};

  void validate() const {
    detail::validate_identifier(installation_id, "installation_id", 256);
  }

  auto operator==(const BotInstallationRef &) const -> bool = default;
};

struct GroupTarget {
  BotInstallationRef installation;
  std::string native_group_id;

  void validate() const {
    installation.validate();
    detail::validate_identifier(native_group_id, "native_group_id");
  }

  auto operator==(const GroupTarget &) const -> bool = default;
};

struct TelegramTopicTarget {
  GroupTarget group;
  std::int64_t topic_id{};

  void validate() const {
    group.validate();
    if (group.installation.surface != BotSurface::TelegramBotApi) {
      throw std::invalid_argument(
          "TelegramTopicTarget requires telegram.bot_api");
    }
    if (topic_id <= 0) {
      throw std::invalid_argument("Telegram topic_id must be positive");
    }
  }

  auto operator==(const TelegramTopicTarget &) const -> bool = default;
};

struct BotMessageRef {
  GroupTarget group;
  std::string native_message_id;

  void validate() const {
    group.validate();
    detail::validate_identifier(native_message_id, "native_message_id");
  }

  auto operator==(const BotMessageRef &) const -> bool = default;
};

struct BotOperationError {
  BotOperationErrorCode code{BotOperationErrorCode::InvalidRequest};
  std::string message;
  std::optional<std::string> provider_code;
  std::optional<std::chrono::milliseconds> retry_after;
  bool retryable{};
  SubmissionSafety submission_safety{SubmissionSafety::DefinitelyNotSubmitted};

  void validate() const {
    detail::validate_identifier(message, "error message", 2048);
    if (redact_bot_diagnostic(message) != message) {
      throw std::invalid_argument("error message must be redacted");
    }
    if (provider_code.has_value()) {
      detail::validate_identifier(*provider_code, "provider_code", 256);
      if (redact_bot_diagnostic(*provider_code) != *provider_code) {
        throw std::invalid_argument("provider_code must be redacted");
      }
    }
    if (retry_after.has_value() && retry_after->count() < 0) {
      throw std::invalid_argument("retry_after cannot be negative");
    }
    if (code == BotOperationErrorCode::OutcomeUnknown &&
        submission_safety != SubmissionSafety::PossiblySubmitted) {
      throw std::invalid_argument("outcome_unknown must be possibly submitted");
    }
  }

  auto operator==(const BotOperationError &) const -> bool = default;
};

template <typename T> struct BotOperationResult {
  std::optional<T> value;
  std::optional<BotOperationError> error;

  [[nodiscard]] static auto success(T result) -> BotOperationResult {
    return {.value = std::move(result)};
  }

  [[nodiscard]] static auto failure(BotOperationError failure)
      -> BotOperationResult {
    failure.validate();
    return {.error = std::move(failure)};
  }

  [[nodiscard]] auto ok() const noexcept -> bool {
    return value.has_value() && !error.has_value();
  }

  void validate() const {
    if (value.has_value() == error.has_value()) {
      throw std::invalid_argument(
          "BotOperationResult requires exactly one value or error");
    }
    if (error.has_value()) {
      error->validate();
    }
  }

  auto operator==(const BotOperationResult &) const -> bool = default;
};

inline void to_json(Json &document, const BotSurface surface) {
  document = surface_id(surface);
}

inline void from_json(const Json &document, BotSurface &surface) {
  if (!document.is_string()) {
    throw std::invalid_argument("BotSurface must be a string");
  }
  surface = bot_surface_from_id(document.get<std::string>());
}

inline void to_json(Json &document, const BotAction action) {
  document = action_id(action);
}

inline void from_json(const Json &document, BotAction &action) {
  if (!document.is_string()) {
    throw std::invalid_argument("BotAction must be a string");
  }
  action = bot_action_from_id(document.get<std::string>());
}

inline void to_json(Json &document, const SubmissionSafety safety) {
  document = submission_safety_id(safety);
}

inline void from_json(const Json &document, SubmissionSafety &safety) {
  if (!document.is_string()) {
    throw std::invalid_argument("SubmissionSafety must be a string");
  }
  safety = submission_safety_from_id(document.get<std::string>());
}

inline void to_json(Json &document, const BotOperationErrorCode code) {
  document = error_code_id(code);
}

inline void from_json(const Json &document, BotOperationErrorCode &code) {
  if (!document.is_string()) {
    throw std::invalid_argument("BotOperationErrorCode must be a string");
  }
  code = error_code_from_id(document.get<std::string>());
}

inline void to_json(Json &document, const BotInstallationRef &installation) {
  installation.validate();
  document = {{"installation_id", installation.installation_id},
              {"surface", installation.surface}};
}

inline void from_json(const Json &document, BotInstallationRef &installation) {
  detail::require_object(document, "BotInstallationRef");
  installation.installation_id =
      detail::require_string(document, "installation_id", "BotInstallationRef");
  if (!document.contains("surface")) {
    throw std::invalid_argument("BotInstallationRef requires surface");
  }
  installation.surface = document.at("surface").get<BotSurface>();
  installation.validate();
}

inline void to_json(Json &document, const GroupTarget &target) {
  target.validate();
  document = {{"installation", target.installation},
              {"native_group_id", target.native_group_id}};
}

inline void from_json(const Json &document, GroupTarget &target) {
  detail::require_object(document, "GroupTarget");
  if (!document.contains("installation")) {
    throw std::invalid_argument("GroupTarget requires installation");
  }
  target.installation = document.at("installation").get<BotInstallationRef>();
  target.native_group_id =
      detail::require_string(document, "native_group_id", "GroupTarget");
  target.validate();
}

inline void to_json(Json &document, const TelegramTopicTarget &target) {
  target.validate();
  document = {{"group", target.group}, {"topic_id", target.topic_id}};
}

inline void from_json(const Json &document, TelegramTopicTarget &target) {
  detail::require_object(document, "TelegramTopicTarget");
  if (!document.contains("group") || !document.at("group").is_object()) {
    throw std::invalid_argument("TelegramTopicTarget requires group");
  }
  if (!document.contains("topic_id") ||
      !document.at("topic_id").is_number_integer()) {
    throw std::invalid_argument(
        "TelegramTopicTarget requires integer topic_id");
  }
  target.group = document.at("group").get<GroupTarget>();
  target.topic_id = document.at("topic_id").get<std::int64_t>();
  target.validate();
}

inline void to_json(Json &document, const BotMessageRef &message) {
  message.validate();
  document = {{"group", message.group},
              {"native_message_id", message.native_message_id}};
}

inline void from_json(const Json &document, BotMessageRef &message) {
  detail::require_object(document, "BotMessageRef");
  if (!document.contains("group") || !document.at("group").is_object()) {
    throw std::invalid_argument("BotMessageRef requires group");
  }
  message.group = document.at("group").get<GroupTarget>();
  message.native_message_id =
      detail::require_string(document, "native_message_id", "BotMessageRef");
  message.validate();
}

inline void to_json(Json &document, const BotOperationError &error) {
  error.validate();
  document = {{"code", error.code},
              {"message", error.message},
              {"retryable", error.retryable},
              {"submission_safety", error.submission_safety}};
  if (error.provider_code.has_value()) {
    document["provider_code"] = *error.provider_code;
  }
  if (error.retry_after.has_value()) {
    document["retry_after_ms"] = error.retry_after->count();
  }
}

inline void from_json(const Json &document, BotOperationError &error) {
  detail::require_object(document, "BotOperationError");
  if (!document.contains("code") || !document.contains("submission_safety")) {
    throw std::invalid_argument(
        "BotOperationError requires code and submission_safety");
  }
  error.code = document.at("code").get<BotOperationErrorCode>();
  error.message =
      detail::require_string(document, "message", "BotOperationError");
  if (!document.contains("retryable") ||
      !document.at("retryable").is_boolean()) {
    throw std::invalid_argument("BotOperationError requires boolean retryable");
  }
  error.retryable = document.at("retryable").get<bool>();
  error.submission_safety =
      document.at("submission_safety").get<SubmissionSafety>();
  error.provider_code.reset();
  if (document.contains("provider_code")) {
    error.provider_code =
        detail::require_string(document, "provider_code", "BotOperationError");
  }
  error.retry_after.reset();
  if (document.contains("retry_after_ms")) {
    if (!document.at("retry_after_ms").is_number_integer()) {
      throw std::invalid_argument(
          "BotOperationError retry_after_ms must be an integer");
    }
    error.retry_after = std::chrono::milliseconds{
        document.at("retry_after_ms").get<std::int64_t>()};
  }
  error.validate();
}

template <typename T>
void to_json(Json &document, const BotOperationResult<T> &result) {
  result.validate();
  if (result.ok()) {
    document = {{"ok", true}, {"value", *result.value}};
  } else {
    document = {{"ok", false}, {"error", *result.error}};
  }
}

template <typename T>
void from_json(const Json &document, BotOperationResult<T> &result) {
  detail::require_object(document, "BotOperationResult");
  if (!document.contains("ok") || !document.at("ok").is_boolean()) {
    throw std::invalid_argument("BotOperationResult requires boolean ok");
  }
  const auto succeeded = document.at("ok").get<bool>();
  result.value.reset();
  result.error.reset();
  if (succeeded) {
    if (!document.contains("value")) {
      throw std::invalid_argument(
          "successful BotOperationResult requires value");
    }
    result.value = document.at("value").get<T>();
  } else {
    if (!document.contains("error")) {
      throw std::invalid_argument("failed BotOperationResult requires error");
    }
    result.error = document.at("error").get<BotOperationError>();
  }
  result.validate();
}

} // namespace obcx::bot
