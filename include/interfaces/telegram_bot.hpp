#pragma once

#include "common/message_type.hpp"

#include <boost/asio/awaitable.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace obcx::core {

struct MediaFileInfo {
  std::string file_id;
  std::string file_unique_id;
  std::string file_type;
  std::optional<int64_t> file_size;
  std::optional<std::string> mime_type;
  std::optional<std::string> file_name;
};

struct TelegramMediaUpload {
  std::string type;
  std::string filename;
  std::string mime_type;
  std::string data;
};

// Telegram measures message-entity offsets and lengths in UTF-16 code units.
struct TelegramTextEntity {
  std::string type;
  std::size_t offset{0};
  std::size_t length{0};
};

inline void to_json(nlohmann::json &json, const TelegramTextEntity &entity) {
  json = {{"type", entity.type},
          {"offset", entity.offset},
          {"length", entity.length}};
}

[[nodiscard]] inline auto telegram_utf16_code_units(std::string_view text)
    -> std::size_t {
  std::size_t units = 0;
  for (std::size_t index = 0; index < text.size();) {
    const auto lead = static_cast<unsigned char>(text[index]);
    std::size_t width = 1;
    if ((lead & 0xE0U) == 0xC0U) {
      width = 2;
    } else if ((lead & 0xF0U) == 0xE0U) {
      width = 3;
    } else if ((lead & 0xF8U) == 0xF0U) {
      width = 4;
    }
    if (index + width > text.size()) {
      width = 1;
    }
    units += width == 4 ? 2U : 1U;
    index += width;
  }
  return units;
}

// Optional Telegram capability for media groups whose bytes are uploaded in
// the request instead of being fetched by Telegram from remote URLs.
class ITelegramMediaGroupUploader {
public:
  ITelegramMediaGroupUploader() = default;
  ITelegramMediaGroupUploader(const ITelegramMediaGroupUploader &) = delete;
  auto operator=(const ITelegramMediaGroupUploader &)
      -> ITelegramMediaGroupUploader & = delete;
  ITelegramMediaGroupUploader(ITelegramMediaGroupUploader &&) = delete;
  auto operator=(ITelegramMediaGroupUploader &&)
      -> ITelegramMediaGroupUploader & = delete;
  virtual ~ITelegramMediaGroupUploader() = default;

  virtual auto send_media_group_uploads(
      std::string_view chat_id, const std::vector<TelegramMediaUpload> &media,
      std::string_view caption = "",
      std::optional<int64_t> topic_id = std::nullopt,
      std::optional<std::string> reply_to_message_id = std::nullopt)
      -> boost::asio::awaitable<std::string> = 0;

  virtual auto send_media_group_uploads_with_entities(
      std::string_view chat_id, const std::vector<TelegramMediaUpload> &media,
      std::string_view caption, std::optional<int64_t> topic_id,
      std::optional<std::string> reply_to_message_id,
      const std::vector<TelegramTextEntity> &caption_entities)
      -> boost::asio::awaitable<std::string> {
    (void)caption_entities;
    co_return co_await send_media_group_uploads(
        chat_id, media, caption, topic_id, std::move(reply_to_message_id));
  }
};

// Stable Telegram-specific capability surface available to actor packages.
// Actors discover it from an IBot reference with dynamic_cast and never need
// concrete TGBot or connection-manager implementation headers.
class ITelegramBot {
public:
  ITelegramBot() = default;
  ITelegramBot(const ITelegramBot &) = delete;
  auto operator=(const ITelegramBot &) -> ITelegramBot & = delete;
  ITelegramBot(ITelegramBot &&) = delete;
  auto operator=(ITelegramBot &&) -> ITelegramBot & = delete;
  virtual ~ITelegramBot() = default;

  virtual auto send_topic_message(std::string_view group_id, int64_t topic_id,
                                  const common::Message &message)
      -> boost::asio::awaitable<std::string> = 0;

  virtual auto send_group_photo(std::string_view group_id,
                                std::string_view photo_data,
                                std::string_view caption = "")
      -> boost::asio::awaitable<std::string> = 0;

  virtual auto send_media_group(
      std::string_view chat_id,
      const std::vector<std::pair<std::string, std::string>> &media,
      std::string_view caption = "",
      std::optional<int64_t> topic_id = std::nullopt,
      std::optional<std::string> reply_to_message_id = std::nullopt)
      -> boost::asio::awaitable<std::string> = 0;

  virtual auto edit_message_text(std::string_view chat_id,
                                 std::string_view message_id,
                                 std::string_view text,
                                 std::string_view parse_mode = "")
      -> boost::asio::awaitable<std::string> = 0;

  virtual auto set_commands(
      const std::vector<std::pair<std::string, std::string>> &commands)
      -> boost::asio::awaitable<std::string> {
    (void)commands;
    co_return "{}";
  }

  virtual auto get_media_download_url(const MediaFileInfo &media_info)
      -> boost::asio::awaitable<std::optional<std::string>> = 0;

  virtual auto get_media_download_urls(
      const std::vector<MediaFileInfo> &media_list)
      -> boost::asio::awaitable<std::vector<std::optional<std::string>>> = 0;

  virtual auto download_file_content(std::string_view download_url)
      -> boost::asio::awaitable<std::string> = 0;

  virtual auto send_group_photo_with_entities(
      std::string_view group_id, std::string_view photo_data,
      std::string_view caption,
      const std::vector<TelegramTextEntity> &caption_entities)
      -> boost::asio::awaitable<std::string> {
    (void)caption_entities;
    co_return co_await send_group_photo(group_id, photo_data, caption);
  }

  virtual auto send_media_group_with_entities(
      std::string_view chat_id,
      const std::vector<std::pair<std::string, std::string>> &media,
      std::string_view caption, std::optional<int64_t> topic_id,
      std::optional<std::string> reply_to_message_id,
      const std::vector<TelegramTextEntity> &caption_entities)
      -> boost::asio::awaitable<std::string> {
    (void)caption_entities;
    co_return co_await send_media_group(chat_id, media, caption, topic_id,
                                        std::move(reply_to_message_id));
  }
};

} // namespace obcx::core
