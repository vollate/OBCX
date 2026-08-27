#ifndef OBCX_INCLUDE_CORE_TELEGRAM_BOT_OPERATIONS_HPP_
#define OBCX_INCLUDE_CORE_TELEGRAM_BOT_OPERATIONS_HPP_

#include "core/bot_operations.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace obcx::bot {

inline constexpr std::size_t maximum_actor_media_bytes =
    std::size_t{128} * 1024U * 1024U;

namespace detail {

inline void require_telegram(const BotInstallationRef &installation,
                             const std::string_view type) {
  installation.validate();
  if (installation.surface != BotSurface::TelegramBotApi) {
    throw std::invalid_argument(std::string{type} +
                                " requires telegram.bot_api");
  }
}

inline void validate_optional_topic(const std::optional<std::int64_t> topic_id,
                                    const std::string_view type) {
  if (topic_id.has_value() && *topic_id <= 0) {
    throw std::invalid_argument(std::string{type} +
                                " topic_id must be positive");
  }
}

inline void validate_media_bound(const std::size_t maximum_bytes,
                                 const std::string_view type) {
  if (maximum_bytes == 0 || maximum_bytes > maximum_actor_media_bytes) {
    throw std::invalid_argument(std::string{type} +
                                " maximum_bytes is outside the SDK bound");
  }
}

inline auto optional_topic_from_json(const Json &document)
    -> std::optional<std::int64_t> {
  if (!document.contains("topic_id") || document.at("topic_id").is_null()) {
    return std::nullopt;
  }
  if (!document.at("topic_id").is_number_integer()) {
    throw std::invalid_argument("topic_id must be an integer");
  }
  return document.at("topic_id").get<std::int64_t>();
}

inline auto optional_reply_from_json(const Json &document)
    -> std::optional<BotMessageRef> {
  if (!document.contains("reply_to") || document.at("reply_to").is_null()) {
    return std::nullopt;
  }
  if (!document.at("reply_to").is_object()) {
    throw std::invalid_argument("reply_to must be a BotMessageRef");
  }
  return document.at("reply_to").get<BotMessageRef>();
}

} // namespace detail

struct TelegramTextEntity {
  std::string type;
  std::size_t offset{};
  std::size_t length{};

  void validate() const {
    detail::validate_identifier(type, "Telegram entity type", 128);
    if (length == 0) {
      throw std::invalid_argument("Telegram entity length must be positive");
    }
    if (offset > std::numeric_limits<std::size_t>::max() - length) {
      throw std::invalid_argument("Telegram entity range overflows");
    }
  }

  auto operator==(const TelegramTextEntity &) const -> bool = default;
};

struct TelegramMediaSource {
  std::string type;
  std::string source;

  void validate() const {
    detail::validate_identifier(type, "Telegram media type", 64);
    detail::validate_identifier(source, "Telegram media source", 16384);
  }

  auto operator==(const TelegramMediaSource &) const -> bool = default;
};

struct TelegramMediaUpload {
  std::string type;
  std::string filename;
  std::string mime_type;
  std::vector<std::uint8_t> bytes;

  void validate() const { validate(maximum_actor_media_bytes); }

  void validate(const std::size_t maximum_bytes) const {
    detail::validate_identifier(type, "Telegram upload type", 64);
    detail::validate_identifier(filename, "Telegram upload filename", 512);
    detail::validate_identifier(mime_type, "Telegram upload MIME type", 256);
    if (bytes.empty()) {
      throw std::invalid_argument("Telegram upload bytes cannot be empty");
    }
    if (bytes.size() > maximum_bytes) {
      throw std::invalid_argument("Telegram upload exceeds request byte bound");
    }
  }

  auto operator==(const TelegramMediaUpload &) const -> bool = default;
};

struct TelegramFileRef {
  std::string file_id;
  std::string file_unique_id;
  std::string file_type;
  std::optional<std::int64_t> file_size;
  std::optional<std::string> mime_type;
  std::optional<std::string> file_name;

  void validate() const {
    detail::validate_identifier(file_id, "Telegram file_id", 2048);
    if (!file_unique_id.empty()) {
      detail::validate_identifier(file_unique_id, "Telegram file_unique_id",
                                  2048);
    }
    detail::validate_identifier(file_type, "Telegram file_type", 64);
    if (file_size.has_value() && *file_size < 0) {
      throw std::invalid_argument("Telegram file_size cannot be negative");
    }
    if (mime_type.has_value()) {
      detail::validate_identifier(*mime_type, "Telegram file MIME type", 256);
    }
    if (file_name.has_value()) {
      detail::validate_identifier(*file_name, "Telegram file name", 512);
    }
  }

  auto operator==(const TelegramFileRef &) const -> bool = default;
};

struct SendTelegramPhotoRequest {
  static constexpr BotAction action = BotAction::SendTelegramPhoto;

  GroupTarget target;
  std::string photo;
  std::string caption;
  std::vector<TelegramTextEntity> caption_entities;

  void validate() const {
    target.validate();
    detail::require_telegram(target.installation, "SendTelegramPhotoRequest");
    detail::validate_identifier(photo, "Telegram photo source", 16384);
    if (caption.size() > 65536U) {
      throw std::invalid_argument("Telegram photo caption exceeds SDK bound");
    }
    for (const auto &entity : caption_entities) {
      entity.validate();
    }
  }
};

struct SendTelegramMediaGroupUrlsRequest {
  static constexpr BotAction action = BotAction::SendTelegramMediaGroupUrls;

  GroupTarget target;
  std::vector<TelegramMediaSource> media;
  std::string caption;
  std::optional<std::int64_t> topic_id;
  std::optional<BotMessageRef> reply_to;
  std::vector<TelegramTextEntity> caption_entities;

  void validate() const {
    target.validate();
    detail::require_telegram(target.installation,
                             "SendTelegramMediaGroupUrlsRequest");
    if (media.size() < 2U || media.size() > 10U) {
      throw std::invalid_argument(
          "Telegram URL media group requires between 2 and 10 items");
    }
    for (const auto &item : media) {
      item.validate();
    }
    if (caption.size() > 65536U) {
      throw std::invalid_argument(
          "Telegram media-group caption exceeds SDK bound");
    }
    detail::validate_optional_topic(topic_id,
                                    "SendTelegramMediaGroupUrlsRequest");
    if (reply_to.has_value()) {
      reply_to->validate();
      if (reply_to->group != target) {
        throw std::invalid_argument(
            "Telegram media-group reply must belong to its target group");
      }
    }
    for (const auto &entity : caption_entities) {
      entity.validate();
    }
  }
};

struct SendTelegramMediaGroupUploadsRequest {
  static constexpr BotAction action = BotAction::SendTelegramMediaGroupUploads;

  GroupTarget target;
  std::vector<TelegramMediaUpload> media;
  std::string caption;
  std::optional<std::int64_t> topic_id;
  std::optional<BotMessageRef> reply_to;
  std::vector<TelegramTextEntity> caption_entities;
  std::size_t maximum_bytes{maximum_actor_media_bytes};

  void validate() const {
    target.validate();
    detail::require_telegram(target.installation,
                             "SendTelegramMediaGroupUploadsRequest");
    detail::validate_media_bound(maximum_bytes,
                                 "SendTelegramMediaGroupUploadsRequest");
    if (media.size() < 2U || media.size() > 10U) {
      throw std::invalid_argument(
          "Telegram upload media group requires between 2 and 10 items");
    }
    std::size_t total = 0;
    for (const auto &item : media) {
      item.validate(maximum_bytes);
      if (item.bytes.size() > maximum_bytes - total) {
        throw std::invalid_argument(
            "Telegram uploads exceed request byte bound");
      }
      total += item.bytes.size();
    }
    if (caption.size() > 65536U) {
      throw std::invalid_argument(
          "Telegram media-group caption exceeds SDK bound");
    }
    detail::validate_optional_topic(topic_id,
                                    "SendTelegramMediaGroupUploadsRequest");
    if (reply_to.has_value()) {
      reply_to->validate();
      if (reply_to->group != target) {
        throw std::invalid_argument(
            "Telegram media-group reply must belong to its target group");
      }
    }
    for (const auto &entity : caption_entities) {
      entity.validate();
    }
  }
};

struct FetchTelegramFileRequest {
  static constexpr BotAction action = BotAction::FetchTelegramFile;

  BotInstallationRef installation;
  TelegramFileRef file;
  std::size_t maximum_bytes{};

  void validate() const {
    detail::require_telegram(installation, "FetchTelegramFileRequest");
    file.validate();
    detail::validate_media_bound(maximum_bytes, "FetchTelegramFileRequest");
    if (file.file_size.has_value() &&
        static_cast<std::uint64_t>(*file.file_size) > maximum_bytes) {
      throw std::invalid_argument(
          "Telegram declared file size exceeds request byte bound");
    }
  }
};

struct FetchedTelegramFile {
  BotInstallationRef installation;
  TelegramFileRef file;
  std::vector<std::uint8_t> bytes;

  void validate() const {
    detail::require_telegram(installation, "FetchedTelegramFile");
    file.validate();
    if (bytes.empty()) {
      throw std::invalid_argument("fetched Telegram file cannot be empty");
    }
    if (bytes.size() > maximum_actor_media_bytes) {
      throw std::invalid_argument("fetched Telegram file exceeds SDK bound");
    }
  }

  auto operator==(const FetchedTelegramFile &) const -> bool = default;
};

inline void to_json(Json &document, const TelegramTextEntity &entity) {
  entity.validate();
  document = {{"type", entity.type},
              {"offset", entity.offset},
              {"length", entity.length}};
}

inline void from_json(const Json &document, TelegramTextEntity &entity) {
  detail::require_object(document, "TelegramTextEntity");
  entity.type = detail::require_string(document, "type", "TelegramTextEntity");
  if (!document.contains("offset") ||
      !document.at("offset").is_number_unsigned() ||
      !document.contains("length") ||
      !document.at("length").is_number_unsigned()) {
    throw std::invalid_argument(
        "TelegramTextEntity requires unsigned offset and length");
  }
  entity.offset = document.at("offset").get<std::size_t>();
  entity.length = document.at("length").get<std::size_t>();
  entity.validate();
}

inline void to_json(Json &document, const TelegramMediaSource &media) {
  media.validate();
  document = {{"type", media.type}, {"source", media.source}};
}

inline void from_json(const Json &document, TelegramMediaSource &media) {
  detail::require_object(document, "TelegramMediaSource");
  media.type = detail::require_string(document, "type", "TelegramMediaSource");
  media.source =
      detail::require_string(document, "source", "TelegramMediaSource");
  media.validate();
}

inline void to_json(Json &document, const TelegramMediaUpload &media) {
  media.validate();
  document = {{"type", media.type},
              {"filename", media.filename},
              {"mime_type", media.mime_type},
              {"bytes", media.bytes}};
}

inline void from_json(const Json &document, TelegramMediaUpload &media) {
  detail::require_object(document, "TelegramMediaUpload");
  media.type = detail::require_string(document, "type", "TelegramMediaUpload");
  media.filename =
      detail::require_string(document, "filename", "TelegramMediaUpload");
  media.mime_type =
      detail::require_string(document, "mime_type", "TelegramMediaUpload");
  if (!document.contains("bytes") || !document.at("bytes").is_array()) {
    throw std::invalid_argument("TelegramMediaUpload requires bytes array");
  }
  media.bytes = document.at("bytes").get<std::vector<std::uint8_t>>();
  media.validate();
}

inline void to_json(Json &document, const TelegramFileRef &file) {
  file.validate();
  document = {{"file_id", file.file_id},
              {"file_unique_id", file.file_unique_id},
              {"file_type", file.file_type}};
  if (file.file_size.has_value()) {
    document["file_size"] = *file.file_size;
  }
  if (file.mime_type.has_value()) {
    document["mime_type"] = *file.mime_type;
  }
  if (file.file_name.has_value()) {
    document["file_name"] = *file.file_name;
  }
}

inline void from_json(const Json &document, TelegramFileRef &file) {
  detail::require_object(document, "TelegramFileRef");
  file.file_id = detail::require_string(document, "file_id", "TelegramFileRef");
  file.file_unique_id.clear();
  if (document.contains("file_unique_id")) {
    file.file_unique_id =
        detail::require_string(document, "file_unique_id", "TelegramFileRef");
  }
  file.file_type =
      detail::require_string(document, "file_type", "TelegramFileRef");
  file.file_size.reset();
  if (document.contains("file_size")) {
    if (!document.at("file_size").is_number_integer()) {
      throw std::invalid_argument("TelegramFileRef file_size must be integer");
    }
    file.file_size = document.at("file_size").get<std::int64_t>();
  }
  file.mime_type.reset();
  if (document.contains("mime_type")) {
    file.mime_type =
        detail::require_string(document, "mime_type", "TelegramFileRef");
  }
  file.file_name.reset();
  if (document.contains("file_name")) {
    file.file_name =
        detail::require_string(document, "file_name", "TelegramFileRef");
  }
  file.validate();
}

inline void to_json(Json &document, const SendTelegramPhotoRequest &request) {
  request.validate();
  document = {{"action", request.action},
              {"target", request.target},
              {"photo", request.photo},
              {"caption", request.caption},
              {"caption_entities", request.caption_entities}};
}

inline void from_json(const Json &document, SendTelegramPhotoRequest &request) {
  detail::require_object(document, "SendTelegramPhotoRequest");
  if (document.contains("action") &&
      document.at("action").get<BotAction>() != request.action) {
    throw std::invalid_argument("SendTelegramPhotoRequest action mismatch");
  }
  if (!document.contains("target")) {
    throw std::invalid_argument("SendTelegramPhotoRequest requires target");
  }
  request.target = document.at("target").get<GroupTarget>();
  request.photo =
      detail::require_string(document, "photo", "SendTelegramPhotoRequest");
  request.caption.clear();
  if (document.contains("caption")) {
    request.caption =
        detail::require_string(document, "caption", "SendTelegramPhotoRequest");
  }
  request.caption_entities.clear();
  if (document.contains("caption_entities")) {
    request.caption_entities =
        document.at("caption_entities").get<std::vector<TelegramTextEntity>>();
  }
  request.validate();
}

inline void to_json(Json &document,
                    const SendTelegramMediaGroupUrlsRequest &request) {
  request.validate();
  document = {{"action", request.action},
              {"target", request.target},
              {"media", request.media},
              {"caption", request.caption},
              {"caption_entities", request.caption_entities}};
  if (request.topic_id.has_value()) {
    document["topic_id"] = *request.topic_id;
  }
  if (request.reply_to.has_value()) {
    document["reply_to"] = *request.reply_to;
  }
}

inline void from_json(const Json &document,
                      SendTelegramMediaGroupUrlsRequest &request) {
  detail::require_object(document, "SendTelegramMediaGroupUrlsRequest");
  if (document.contains("action") &&
      document.at("action").get<BotAction>() != request.action) {
    throw std::invalid_argument(
        "SendTelegramMediaGroupUrlsRequest action mismatch");
  }
  if (!document.contains("target") || !document.contains("media") ||
      !document.at("media").is_array()) {
    throw std::invalid_argument(
        "SendTelegramMediaGroupUrlsRequest requires target and media");
  }
  request.target = document.at("target").get<GroupTarget>();
  request.media = document.at("media").get<std::vector<TelegramMediaSource>>();
  request.caption.clear();
  if (document.contains("caption")) {
    request.caption = detail::require_string(
        document, "caption", "SendTelegramMediaGroupUrlsRequest");
  }
  request.topic_id = detail::optional_topic_from_json(document);
  request.reply_to = detail::optional_reply_from_json(document);
  request.caption_entities.clear();
  if (document.contains("caption_entities")) {
    request.caption_entities =
        document.at("caption_entities").get<std::vector<TelegramTextEntity>>();
  }
  request.validate();
}

inline void to_json(Json &document,
                    const SendTelegramMediaGroupUploadsRequest &request) {
  request.validate();
  document = {{"action", request.action},
              {"target", request.target},
              {"media", request.media},
              {"caption", request.caption},
              {"caption_entities", request.caption_entities},
              {"maximum_bytes", request.maximum_bytes}};
  if (request.topic_id.has_value()) {
    document["topic_id"] = *request.topic_id;
  }
  if (request.reply_to.has_value()) {
    document["reply_to"] = *request.reply_to;
  }
}

inline void from_json(const Json &document,
                      SendTelegramMediaGroupUploadsRequest &request) {
  detail::require_object(document, "SendTelegramMediaGroupUploadsRequest");
  if (document.contains("action") &&
      document.at("action").get<BotAction>() != request.action) {
    throw std::invalid_argument(
        "SendTelegramMediaGroupUploadsRequest action mismatch");
  }
  if (!document.contains("target") || !document.contains("media") ||
      !document.at("media").is_array() || !document.contains("maximum_bytes") ||
      !document.at("maximum_bytes").is_number_unsigned()) {
    throw std::invalid_argument("SendTelegramMediaGroupUploadsRequest requires "
                                "target, media, and maximum_bytes");
  }
  request.target = document.at("target").get<GroupTarget>();
  request.media = document.at("media").get<std::vector<TelegramMediaUpload>>();
  request.caption.clear();
  if (document.contains("caption")) {
    request.caption = detail::require_string(
        document, "caption", "SendTelegramMediaGroupUploadsRequest");
  }
  request.topic_id = detail::optional_topic_from_json(document);
  request.reply_to = detail::optional_reply_from_json(document);
  request.caption_entities.clear();
  if (document.contains("caption_entities")) {
    request.caption_entities =
        document.at("caption_entities").get<std::vector<TelegramTextEntity>>();
  }
  request.maximum_bytes = document.at("maximum_bytes").get<std::size_t>();
  request.validate();
}

inline void to_json(Json &document, const FetchTelegramFileRequest &request) {
  request.validate();
  document = {{"action", request.action},
              {"installation", request.installation},
              {"file", request.file},
              {"maximum_bytes", request.maximum_bytes}};
}

inline void from_json(const Json &document, FetchTelegramFileRequest &request) {
  detail::require_object(document, "FetchTelegramFileRequest");
  if (document.contains("action") &&
      document.at("action").get<BotAction>() != request.action) {
    throw std::invalid_argument("FetchTelegramFileRequest action mismatch");
  }
  if (!document.contains("installation") || !document.contains("file") ||
      !document.contains("maximum_bytes") ||
      !document.at("maximum_bytes").is_number_unsigned()) {
    throw std::invalid_argument("FetchTelegramFileRequest requires "
                                "installation, file, and maximum_bytes");
  }
  request.installation = document.at("installation").get<BotInstallationRef>();
  request.file = document.at("file").get<TelegramFileRef>();
  request.maximum_bytes = document.at("maximum_bytes").get<std::size_t>();
  request.validate();
}

inline void to_json(Json &document, const FetchedTelegramFile &file) {
  file.validate();
  document = {{"installation", file.installation},
              {"file", file.file},
              {"bytes", file.bytes}};
}

inline void from_json(const Json &document, FetchedTelegramFile &file) {
  detail::require_object(document, "FetchedTelegramFile");
  if (!document.contains("installation") || !document.contains("file") ||
      !document.contains("bytes") || !document.at("bytes").is_array()) {
    throw std::invalid_argument(
        "FetchedTelegramFile requires installation, file, and bytes array");
  }
  file.installation = document.at("installation").get<BotInstallationRef>();
  file.file = document.at("file").get<TelegramFileRef>();
  file.bytes = document.at("bytes").get<std::vector<std::uint8_t>>();
  file.validate();
}

} // namespace obcx::bot

#endif // OBCX_INCLUDE_CORE_TELEGRAM_BOT_OPERATIONS_HPP_
