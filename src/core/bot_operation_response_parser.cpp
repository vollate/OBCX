#include "core/bot_operation_response_parser.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace obcx::core {
namespace {

using bot::BotOperationError;
using bot::BotOperationErrorCode;
using bot::BotOperationResult;
using bot::Json;
using bot::SubmissionSafety;

auto scalar_string(const Json &value) -> std::optional<std::string> {
  if (value.is_string()) {
    return value.get<std::string>();
  }
  if (value.is_number_integer()) {
    return std::to_string(value.get<std::int64_t>());
  }
  if (value.is_number_unsigned()) {
    return std::to_string(value.get<std::uint64_t>());
  }
  return std::nullopt;
}

auto safe_message(const Json &document, const std::string_view primary,
                  const std::string_view secondary,
                  const std::string_view fallback) -> std::string {
  for (const auto key : {primary, secondary}) {
    const auto field = std::string{key};
    if (document.contains(field) && document.at(field).is_string()) {
      const auto value = document.at(field).get<std::string>();
      if (!value.empty()) {
        return bot::redact_bot_diagnostic(value);
      }
    }
  }
  return std::string{fallback};
}

auto malformed(const std::string_view provider, const bool side_effecting)
    -> BotOperationResult<Json> {
  return BotOperationResult<Json>::failure(
      {.code = BotOperationErrorCode::MalformedResponse,
       .message = std::string{provider} +
                  " returned an invalid or incomplete response",
       .retryable = !side_effecting,
       .submission_safety = side_effecting
                                ? SubmissionSafety::PossiblySubmitted
                                : SubmissionSafety::DefinitelyNotSubmitted});
}

auto telegram_retry_after(const Json &document)
    -> std::optional<std::chrono::milliseconds> {
  if (!document.contains("parameters") ||
      !document.at("parameters").is_object()) {
    return std::nullopt;
  }
  const auto &parameters = document.at("parameters");
  if (!parameters.contains("retry_after") ||
      !parameters.at("retry_after").is_number_integer()) {
    return std::nullopt;
  }
  const auto seconds = parameters.at("retry_after").get<std::int64_t>();
  if (seconds < 0 ||
      seconds > std::numeric_limits<std::int64_t>::max() / 1000) {
    return std::nullopt;
  }
  return std::chrono::milliseconds{seconds * 1000};
}

auto telegram_rejected(const Json &document) -> BotOperationResult<Json> {
  std::optional<std::string> provider_code;
  std::int64_t numeric_code = 0;
  if (document.contains("error_code")) {
    provider_code = scalar_string(document.at("error_code"));
    if (document.at("error_code").is_number_integer()) {
      numeric_code = document.at("error_code").get<std::int64_t>();
    }
  }
  const auto retry_after = telegram_retry_after(document);
  const bool retryable = retry_after.has_value() || numeric_code == 408 ||
                         numeric_code == 429 || numeric_code >= 500;
  BotOperationError error{
      .code = BotOperationErrorCode::ProviderRejected,
      .message = safe_message(document, "description", "message",
                              "Telegram rejected the operation"),
      .provider_code = std::move(provider_code),
      .retry_after = retry_after,
      .retryable = retryable,
      .submission_safety = SubmissionSafety::DefinitelyNotSubmitted,
  };
  return BotOperationResult<Json>::failure(std::move(error));
}

auto onebot_rejected(const Json &document) -> BotOperationResult<Json> {
  std::optional<std::string> provider_code;
  std::optional<std::int64_t> numeric_code;
  if (document.contains("retcode")) {
    provider_code = scalar_string(document.at("retcode"));
    if (document.at("retcode").is_number_integer()) {
      numeric_code = document.at("retcode").get<std::int64_t>();
    }
  }
  BotOperationError error{
      .code = BotOperationErrorCode::ProviderRejected,
      .message = safe_message(document, "message", "wording",
                              "OneBot rejected the operation"),
      .provider_code = std::move(provider_code),
      // The current OneBot transport reserves negative retcodes for failures
      // before an action result is accepted; positive provider retcodes remain
      // terminal unless a later tested adapter rule says otherwise.
      .retryable = numeric_code.has_value() && *numeric_code < 0,
      .submission_safety = SubmissionSafety::DefinitelyNotSubmitted,
  };
  return BotOperationResult<Json>::failure(std::move(error));
}

} // namespace

auto parse_telegram_operation_response(const std::string_view response,
                                       const bool side_effecting)
    -> bot::BotOperationResult<bot::Json> {
  if (response.empty()) {
    return malformed("Telegram", side_effecting);
  }
  const auto document = Json::parse(response, nullptr, false);
  if (document.is_discarded() || !document.is_object() ||
      !document.contains("ok") || !document.at("ok").is_boolean()) {
    return malformed("Telegram", side_effecting);
  }
  if (!document.at("ok").get<bool>()) {
    return telegram_rejected(document);
  }
  if (!document.contains("result")) {
    return malformed("Telegram", side_effecting);
  }
  return BotOperationResult<Json>::success(document.at("result"));
}

auto parse_onebot11_operation_response(const std::string_view response,
                                       const bool side_effecting)
    -> bot::BotOperationResult<bot::Json> {
  if (response.empty()) {
    return malformed("OneBot", side_effecting);
  }
  const auto document = Json::parse(response, nullptr, false);
  if (document.is_discarded() || !document.is_object() ||
      !document.contains("status") || !document.at("status").is_string()) {
    return malformed("OneBot", side_effecting);
  }

  bool retcode_ok = true;
  if (document.contains("retcode")) {
    if (!document.at("retcode").is_number_integer()) {
      return malformed("OneBot", side_effecting);
    }
    retcode_ok = document.at("retcode").get<std::int64_t>() == 0;
  }
  if (document.at("status").get<std::string>() != "ok" || !retcode_ok) {
    return onebot_rejected(document);
  }
  if (!document.contains("data")) {
    return malformed("OneBot", side_effecting);
  }
  return BotOperationResult<Json>::success(document.at("data"));
}

} // namespace obcx::core
