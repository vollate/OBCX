#include "core/tg_bot.hpp"
#include "common/logger.hpp"
#include "interfaces/connection_manager.hpp"
#include "telegram/adapter/protocol_adapter.hpp"
#include "telegram/network/connection_manager.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <nlohmann/json.hpp>

namespace obcx::core {

TGBot::TGBot(adapter::telegram::ProtocolAdapter adapter)
    : IBot{std::make_unique<adapter::telegram::ProtocolAdapter>(
          std::move(adapter))} {
  OBCX_INFO("TelegramBot instance created, all core components initialized");
}

TGBot::~TGBot() { OBCX_INFO("TelegramBot instance destroyed."); }

void TGBot::connect(network::ConnectionManagerFactory::ConnectionType type,
                    const common::ConnectionConfig &config) {
  conection_config_ = config;
  if (type == network::ConnectionManagerFactory::ConnectionType::TelegramHTTP) {
    connection_manager_ = network::ConnectionManagerFactory::create(
        type, *io_context_, *adapter_);
  } else {
    throw std::runtime_error(
        std::string("Telegram Bot only support TelegramHTTP"));
  }

  connection_manager_->set_event_callback(
      [this](const common::Event &event) -> void {
        dispatcher_->dispatch(this, event);
      });

  connection_manager_->connect(config);

  OBCX_INFO("Connecting to {}:{} using {} connection type", config.host,
            config.port, connection_manager_->get_connection_type());
}

void TGBot::run() {
  if (io_context_->stopped()) {
    io_context_->restart();
  }

  OBCX_INFO("TelegramBot starting event loop...");
  io_context_->run();
  OBCX_INFO("TelegramBot event loop ended");
}

void TGBot::stop() {
  OBCX_INFO("Requesting TelegramBot to stop...");

  if (connection_manager_) {
    connection_manager_->disconnect();
  }

  io_context_->stop();
}

auto TGBot::poll_updates() -> asio::awaitable<void> {
  int offset = 0;
  while (is_connected()) {
    bool success = false;
    try {
      auto updates = co_await get_updates(offset, 100);

      OBCX_DEBUG("Received {} updates from Telegram", updates.length());

      offset += 1;

      success = true;
    } catch (const std::exception &e) {
      OBCX_ERROR("Error polling updates: {}", e.what());
    }

    asio::steady_timer timer(*io_context_,
                             std::chrono::seconds(success ? 1 : 5));
    co_await timer.async_wait(asio::use_awaitable);
  }
}

void TGBot::error_notify(std::string_view target_id, std::string_view message,
                         bool is_group) {
  common::ErrorEvent error_event{.error_type = "message_error",
                                 .error_message = std::string(message),
                                 .target_id = std::string(target_id),
                                 .is_group = is_group,
                                 .time = std::chrono::system_clock::now(),
                                 .context = {{"source", "bot_error_handler"}}};

  if (dispatcher_) {
    dispatcher_->dispatch(this, error_event);
  } else {
    OBCX_WARN("Event dispatcher not initialized, cannot dispatch error event");
  }
}

auto TGBot::send_private_message(std::string_view user_id,
                                 const common::Message &message)
    -> asio::awaitable<std::string> {
  ensure_connection_manager();
  auto echo_id = generate_echo_id();
  auto payload = get_telegram_adapter().serialize_send_message_request(
      user_id, message, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto TGBot::send_group_message(std::string_view group_id,
                               const common::Message &message)
    -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload = get_telegram_adapter().serialize_send_message_request(
      group_id, message, echo_id);

  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto TGBot::set_commands(
    const std::vector<std::pair<std::string, std::string>> &commands)
    -> asio::awaitable<std::string> {
  ensure_connection_manager();
  const auto echo_id = generate_echo_id();
  nlohmann::json request{
      {"method", "setMyCommands"},
      {"commands", nlohmann::json::array()},
      {"echo", echo_id},
  };
  for (const auto &[command, description] : commands) {
    request["commands"].push_back(
        {{"command", command}, {"description", description}});
  }
  co_return co_await connection_manager_->send_action_and_wait_async(
      request.dump(), echo_id);
}

auto TGBot::send_topic_message(std::string_view group_id, int64_t topic_id,
                               const common::Message &message)
    -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload = get_telegram_adapter().serialize_send_topic_message_request(
      group_id, message, echo_id, topic_id);

  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto TGBot::send_group_photo(std::string_view group_id,
                             std::string_view photo_data,
                             std::string_view caption)
    -> asio::awaitable<std::string> {
  co_return co_await send_group_photo_with_entities(group_id, photo_data,
                                                    caption, {});
}

auto TGBot::send_group_photo_with_entities(
    std::string_view group_id, std::string_view photo_data,
    std::string_view caption,
    const std::vector<TelegramTextEntity> &caption_entities)
    -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();

  nlohmann::json request;
  request["method"] = "sendPhoto";
  request["chat_id"] = group_id;
  request["photo"] = photo_data;
  if (!caption.empty()) {
    request["caption"] = caption;
  }
  if (!caption_entities.empty()) {
    request["caption_entities"] = caption_entities;
  }
  request["echo"] = echo_id;

  std::string payload = request.dump();
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto TGBot::send_photo_bytes(std::string_view chat_id,
                             const std::string &image_data,
                             std::string_view filename,
                             std::string_view mime_type,
                             std::string_view caption,
                             std::optional<int64_t> topic_id)
    -> asio::awaitable<std::string> {
  ensure_connection_manager();
  auto *tg_conn_mgr = dynamic_cast<network::TelegramConnectionManager *>(
      connection_manager_.get());
  if (!tg_conn_mgr) {
    throw std::runtime_error(
        std::string("ConnectionManager is not TelegramConnectionManager type"));
  }
  co_return co_await tg_conn_mgr->upload_photo_multipart(
      chat_id, image_data, filename, mime_type, caption, topic_id);
}

auto TGBot::send_media_group(
    std::string_view chat_id,
    const std::vector<std::pair<std::string, std::string>> &media,
    std::string_view caption, std::optional<int64_t> topic_id,
    std::optional<std::string> reply_to_message_id)
    -> asio::awaitable<std::string> {
  co_return co_await send_media_group_with_entities(
      chat_id, media, caption, topic_id, std::move(reply_to_message_id), {});
}

auto TGBot::send_media_group_with_entities(
    std::string_view chat_id,
    const std::vector<std::pair<std::string, std::string>> &media,
    std::string_view caption, std::optional<int64_t> topic_id,
    std::optional<std::string> reply_to_message_id,
    const std::vector<TelegramTextEntity> &caption_entities)
    -> asio::awaitable<std::string> {
  ensure_connection_manager();
  auto echo_id = generate_echo_id();
  auto payload =
      get_telegram_adapter().serialize_send_media_group_request_with_entities(
          chat_id, media, caption, topic_id, reply_to_message_id, echo_id,
          caption_entities);

  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto TGBot::send_media_group_uploads(
    std::string_view chat_id, const std::vector<TelegramMediaUpload> &media,
    std::string_view caption, std::optional<int64_t> topic_id,
    std::optional<std::string> reply_to_message_id)
    -> asio::awaitable<std::string> {
  co_return co_await send_media_group_uploads_with_entities(
      chat_id, media, caption, topic_id, std::move(reply_to_message_id), {});
}

auto TGBot::send_media_group_uploads_with_entities(
    std::string_view chat_id, const std::vector<TelegramMediaUpload> &media,
    std::string_view caption, std::optional<int64_t> topic_id,
    std::optional<std::string> reply_to_message_id,
    const std::vector<TelegramTextEntity> &caption_entities)
    -> asio::awaitable<std::string> {
  ensure_connection_manager();
  auto *tg_conn_mgr = dynamic_cast<network::TelegramConnectionManager *>(
      connection_manager_.get());
  if (!tg_conn_mgr) {
    throw std::runtime_error(
        std::string("ConnectionManager is not TelegramConnectionManager type"));
  }
  co_return co_await tg_conn_mgr->upload_media_group_multipart_with_entities(
      chat_id, media, caption, topic_id, reply_to_message_id, caption_entities);
}

auto TGBot::delete_message(std::string_view message_id)
    -> asio::awaitable<std::string> {
  std::string msg_id(message_id);
  size_t pos = msg_id.find(':');
  if (pos == std::string::npos) {
    throw std::invalid_argument("Invalid parameter: {}");
  }

  std::string chat_id = msg_id.substr(0, pos);
  std::string actual_message_id = msg_id.substr(pos + 1);

  auto echo_id = generate_echo_id();
  auto payload = get_telegram_adapter().serialize_delete_message_request(
      chat_id, actual_message_id, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto TGBot::edit_message_text(std::string_view chat_id,
                              std::string_view message_id,
                              std::string_view text,
                              std::string_view parse_mode)
    -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload = get_telegram_adapter().serialize_edit_message_text_request(
      chat_id, message_id, text, parse_mode, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto TGBot::get_message(std::string_view message_id)
    -> asio::awaitable<std::string> {
  OBCX_WARN("TelegramBot::{} not implemented yet", "get_message");
  co_return "{}";
}

auto TGBot::get_friend_list() -> asio::awaitable<std::string> {
  OBCX_WARN("TelegramBot::{} not implemented yet", "get_friend_list");
  co_return "{}";
}

auto TGBot::get_stranger_info(std::string_view user_id, bool no_cache)
    -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload = get_telegram_adapter().serialize_get_user_info_request(
      "", user_id, no_cache, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto TGBot::get_group_list() -> asio::awaitable<std::string> {
  OBCX_WARN("TelegramBot::{} not implemented yet", "get_group_list");
  co_return "{}";
}

auto TGBot::get_group_info(std::string_view group_id, bool no_cache)
    -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload = get_telegram_adapter().serialize_get_chat_info_request(
      group_id, no_cache, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto TGBot::get_group_member_list(std::string_view group_id)
    -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload = get_telegram_adapter().serialize_get_chat_admins_request(
      group_id, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto TGBot::get_group_member_info(std::string_view group_id,
                                  std::string_view user_id, bool no_cache)
    -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload = get_telegram_adapter().serialize_get_chat_member_info_request(
      group_id, user_id, no_cache, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto TGBot::set_group_kick(std::string_view group_id, std::string_view user_id,
                           bool reject_add_request)
    -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload = get_telegram_adapter().serialize_kick_chat_member_request(
      group_id, user_id, reject_add_request, false, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto TGBot::set_group_ban(std::string_view group_id, std::string_view user_id,
                          int32_t duration) -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload = get_telegram_adapter().serialize_ban_chat_member_request(
      group_id, user_id, duration, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto TGBot::set_group_whole_ban(std::string_view group_id, bool enable)
    -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload = get_telegram_adapter().serialize_ban_all_members_request(
      group_id, enable, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto TGBot::set_group_card(std::string_view group_id, std::string_view user_id,
                           std::string_view card)
    -> asio::awaitable<std::string> {
  OBCX_WARN("TelegramBot::{} not implemented yet", "set_group_card");
  co_return "{}";
}

auto TGBot::set_group_leave(std::string_view group_id, bool is_dismiss)
    -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload = get_telegram_adapter().serialize_leave_chat_request(
      group_id, is_dismiss, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto TGBot::set_group_name(std::string_view group_id,
                           std::string_view group_name)
    -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload = get_telegram_adapter().serialize_set_chat_title_request(
      group_id, group_name, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto TGBot::set_group_admin(std::string_view group_id, std::string_view user_id,
                            bool enable) -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload = get_telegram_adapter().serialize_set_chat_admin_request(
      group_id, user_id, enable, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto TGBot::set_group_anonymous_ban(std::string_view group_id,
                                    const std::string &anonymous,
                                    int32_t duration)
    -> asio::awaitable<std::string> {
  OBCX_WARN("TelegramBot::{} not implemented yet", "set_group_anonymous_ban");
  co_return "{}";
}

auto TGBot::set_group_anonymous(std::string_view group_id, bool enable)
    -> asio::awaitable<std::string> {
  OBCX_WARN("TelegramBot::{} not implemented yet", "set_group_anonymous");
  co_return "{}";
}

auto TGBot::set_group_portrait(std::string_view group_id, std::string_view file,
                               bool cache) -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload = get_telegram_adapter().serialize_set_chat_photo_request(
      group_id, file, cache, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto TGBot::get_group_honor_info(std::string_view group_id,
                                 std::string_view type)
    -> asio::awaitable<std::string> {
  OBCX_WARN("TelegramBot::{} not implemented yet", "get_group_honor_info");
  co_return "{}";
}

auto TGBot::get_login_info() -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload =
      get_telegram_adapter().serialize_get_self_info_request(echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto TGBot::get_status() -> asio::awaitable<std::string> {
  OBCX_WARN("TelegramBot::{} not implemented yet", "get_status");
  co_return R"({"retcode": 0, "status": "ok", "data": {"online": true}})";
}

auto TGBot::get_version_info() -> asio::awaitable<std::string> {
  OBCX_WARN("TelegramBot::{} not implemented yet", "get_version_info");
  co_return R"({"retcode": 0, "data": {"version": "TelegramBot/1.0.0"}})";
}

auto TGBot::get_image(std::string_view file) -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload =
      get_telegram_adapter().serialize_download_file_request(file, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto TGBot::get_record(std::string_view file, std::string_view out_format)
    -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload =
      get_telegram_adapter().serialize_download_file_request(file, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto TGBot::can_send_image() -> asio::awaitable<std::string> {
  co_return R"({"retcode": 0, "data": {"yes": true}})";
}

auto TGBot::can_send_record() -> asio::awaitable<std::string> {
  co_return R"({"retcode": 0, "data": {"yes": true}})";
}

auto TGBot::get_cookies(std::string_view domain)
    -> asio::awaitable<std::string> {
  OBCX_WARN("TelegramBot::{} not implemented yet", "get_cookies");
  co_return "{}";
}

auto TGBot::get_csrf_token() -> asio::awaitable<std::string> {
  OBCX_WARN("TelegramBot::{} not implemented yet", "get_csrf_token");
  co_return "{}";
}

auto TGBot::get_credentials(std::string_view domain)
    -> asio::awaitable<std::string> {
  OBCX_WARN("TelegramBot::{} not implemented yet", "get_credentials");
  co_return "{}";
}

auto TGBot::set_friend_add_request(std::string_view flag, bool approve,
                                   std::string_view remark)
    -> asio::awaitable<std::string> {
  OBCX_WARN("TelegramBot::{} not implemented yet", "set_friend_add_request");
  co_return "{}";
}

auto TGBot::set_group_add_request(std::string_view flag,
                                  std::string_view sub_type, bool approve,
                                  std::string_view reason)
    -> asio::awaitable<std::string> {
  OBCX_WARN("TelegramBot::{} not implemented yet", "set_group_add_request");
  co_return "{}";
}

auto TGBot::is_connected() const -> bool {
  return connection_manager_ && connection_manager_->is_connected();
}

auto TGBot::get_updates(int offset, int limit) -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload = get_telegram_adapter().serialize_get_updates_request(
      offset, limit, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto TGBot::generate_echo_id() -> uint64_t {
  static std::atomic<uint64_t> counter{0};
  return counter.fetch_add(1);
}

void TGBot::ensure_connection_manager() const {
  if (!connection_manager_) {
    throw std::runtime_error(
        std::string("Bot not connected, please call connect* methods first"));
  }
}

auto TGBot::get_telegram_adapter() const
    -> adapter::telegram::ProtocolAdapter & {
  return *dynamic_cast<adapter::telegram::ProtocolAdapter *>(&*adapter_);
}

auto TGBot::extract_media_files(const nlohmann::json &message_data)
    -> std::vector<MediaFileInfo> {
  std::vector<MediaFileInfo> media_files;

  try {
    if (message_data.contains("photo") && message_data["photo"].is_array() &&
        !message_data["photo"].empty()) {
      auto photos = message_data["photo"];
      nlohmann::json largest_photo;
      int max_size = 0;

      for (const auto &photo : photos) {
        if (photo.contains("file_size")) {
          int size = photo["file_size"].get<int>();
          if (size > max_size) {
            max_size = size;
            largest_photo = photo;
          }
        } else if (largest_photo.is_null()) {
          largest_photo = photo;
        }
      }

      if (!largest_photo.is_null() && largest_photo.contains("file_id")) {
        MediaFileInfo info;
        info.file_id = largest_photo["file_id"].get<std::string>();

        OBCX_DEBUG("Photo object content: {}", largest_photo.dump());

        info.file_unique_id =
            largest_photo.contains("file_unique_id")
                ? largest_photo["file_unique_id"].get<std::string>()
                : "";

        OBCX_DEBUG("Extracted file_unique_id: '{}' (empty: {})",
                   info.file_unique_id, info.file_unique_id.empty());

        info.file_type = "photo";
        if (largest_photo.contains("file_size")) {
          info.file_size = largest_photo["file_size"].get<int64_t>();
        }
        media_files.push_back(info);
      }
    }

    std::vector<std::string> media_types = {"video",     "audio",   "voice",
                                            "document",  "sticker", "animation",
                                            "video_note"};

    for (const auto &media_type : media_types) {
      if (message_data.contains(media_type) &&
          message_data[media_type].is_object()) {
        auto media_obj = message_data[media_type];
        if (media_obj.contains("file_id")) {
          MediaFileInfo info;
          info.file_id = media_obj["file_id"].get<std::string>();

          OBCX_DEBUG("{} object content: {}", media_type, media_obj.dump());

          info.file_unique_id =
              media_obj.contains("file_unique_id")
                  ? media_obj["file_unique_id"].get<std::string>()
                  : "";

          OBCX_DEBUG("{} extracted file_unique_id: '{}' (empty: {})",
                     media_type, info.file_unique_id,
                     info.file_unique_id.empty());

          info.file_type = media_type;

          if (media_obj.contains("file_size")) {
            info.file_size = media_obj["file_size"].get<int64_t>();
          }

          if (media_obj.contains("mime_type")) {
            info.mime_type = media_obj["mime_type"].get<std::string>();
          }

          if (media_type == "document" && media_obj.contains("file_name")) {
            info.file_name = media_obj["file_name"].get<std::string>();
          }

          media_files.push_back(info);
        }
      }
    }

  } catch (const std::exception &e) {
    OBCX_ERROR("Error extracting media files: {}", e.what());
  }

  return media_files;
}

auto TGBot::get_media_download_url(const MediaFileInfo &media_info)
    -> asio::awaitable<std::optional<std::string>> {
  try {
    ensure_connection_manager();

    auto *tg_conn_mgr =
        dynamic_cast<obcx::network::TelegramConnectionManager *>(
            connection_manager_.get());
    if (!tg_conn_mgr) {
      OBCX_ERROR("ConnectionManager is not TelegramConnectionManager type");
      co_return std::nullopt;
    }

    std::string download_url =
        co_await tg_conn_mgr->download_file(media_info.file_id);
    co_return download_url;

  } catch (const std::exception &e) {
    OBCX_ERROR("Failed to get media download url (file_id: {}, type: {}): {}",
               media_info.file_id, media_info.file_type, e.what());
    co_return std::nullopt;
  }
}

auto TGBot::get_media_download_urls(
    const std::vector<MediaFileInfo> &media_list)
    -> asio::awaitable<std::vector<std::optional<std::string>>> {
  std::vector<std::optional<std::string>> results;
  results.reserve(media_list.size());

  std::vector<asio::awaitable<std::optional<std::string>>> tasks;
  tasks.reserve(media_list.size());

  for (const auto &media_info : media_list) {
    tasks.push_back(get_media_download_url(media_info));
  }

  for (auto &task : tasks) {
    results.push_back(co_await std::move(task));
  }

  co_return results;
}

auto TGBot::download_file_content(std::string_view download_url)
    -> asio::awaitable<std::string> {
  ensure_connection_manager();

  auto *telegram_connection =
      dynamic_cast<network::TelegramConnectionManager *>(
          connection_manager_.get());
  if (!telegram_connection) {
    throw std::runtime_error(
        "ConnectionManager is not TelegramConnectionManager type");
  }

  co_return co_await telegram_connection->download_file_content(download_url);
}

auto TGBot::get_connection_manager() const -> network::IConnectionManager * {
  return connection_manager_.get();
}

} // namespace obcx::core
