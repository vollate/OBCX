#pragma once

#include "json_utils.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace obcx::common {

/**
 * \if CHINESE
 * @brief 消息状态枚举
 * \endif
 * \if ENGLISH
 * @brief Message status enum.
 * \endif
 */
enum class MessageStatus : std::uint8_t { ok, failed, async_ };

/**
 * \if CHINESE
 * @brief 基础响应结构
 * \endif
 * \if ENGLISH
 * @brief Base response structure.
 * \endif
 */
struct BaseResponse {
  MessageStatus status;
  int ret_code;
  std::optional<std::string> message;
  std::optional<std::string> wording;
  json data;

  void to_json(json &j) const;
  void from_json(const json &j);
};

/**
 * \if CHINESE
 * @brief 基础请求结构
 * \endif
 * \if ENGLISH
 * @brief Base request structure
 * \endif
 */
struct BaseRequest {
  std::string action;
  json params;
  std::optional<std::string> echo;

  void to_json(json &j) const;
  void from_json(const json &j);
};

/**
 * \if CHINESE
 * @brief OneBot事件类型
 * \endif
 * \if ENGLISH
 * @brief OneBot event type
 * \endif
 */
enum class EventType : std::uint8_t { message, notice, request, meta_event };

/**
 * \if CHINESE
 * @brief 基础事件结构
 * \endif
 * \if ENGLISH
 * @brief Base event structure
 * \endif
 */
struct BaseEvent {
  EventType type;
  std::chrono::system_clock::time_point time;
  std::string self_id;
  std::string post_type;
  json data;

  void to_json(json &j) const;
  void from_json(const json &j);
};

/**
 * \if CHINESE
 * @brief 消息段类型
 * \endif
 * \if ENGLISH
 * @brief Message segment type
 * \endif
 */
struct MessageSegment {
  std::string type;
  json data;

  void to_json(json &j) const;
  void from_json(const json &j);
};

// ADL hooks used by nlohmann::json for Message and actor-operation DTOs.
void to_json(json &j, const MessageSegment &segment);
void from_json(const json &j, MessageSegment &segment);

/**
 * \if CHINESE
 * @brief 消息类型
 * \endif
 * \if ENGLISH
 * @brief Message type
 * \endif
 */
using Message = std::vector<MessageSegment>;

/**
 * \if CHINESE
 * @brief 消息事件
 * \endif
 * \if ENGLISH
 * @brief Message event
 * \endif
 */
struct MessageEvent : public BaseEvent {
  std::string message_type; // private, group, channel
  std::string sub_type;
  std::string message_id;
  std::string user_id;
  Message message;
  std::string raw_message;
  int32_t font{};

  std::optional<std::string> group_id;
  std::optional<std::string> anonymous;

  std::optional<std::string> guild_id;
  std::optional<std::string> channel_id;

  void to_json(json &j) const;
  void from_json(const json &j);
};

/**
 * \if CHINESE
 * @brief 通知事件
 * \endif
 * \if ENGLISH
 * @brief Notice event
 * \endif
 */
struct NoticeEvent : public BaseEvent {
  std::string notice_type;
  std::string user_id;
  std::optional<std::string> group_id;

  void to_json(json &j) const;
  void from_json(const json &j);
};

/**
 * \if CHINESE
 * @brief 请求事件
 * \endif
 * \if ENGLISH
 * @brief Request event
 * \endif
 */
struct RequestEvent : public BaseEvent {
  std::string request_type;
  std::string user_id;
  std::string comment;
  std::string flag;

  void to_json(json &j) const;
  void from_json(const json &j);
};

/**
 * \if CHINESE
 * @brief 元事件
 * \endif
 * \if ENGLISH
 * @brief Meta event
 * \endif
 */
struct MetaEvent : public BaseEvent {
  std::string meta_event_type;
  std::string sub_type;

  void to_json(json &j) const;
  void from_json(const json &j);
};

/**
 * \if CHINESE
 * @brief 心跳事件
 * \endif
 * \if ENGLISH
 * @brief Heartbeat event
 * \endif
 */
struct HeartbeatEvent : public MetaEvent {
  json status;

  /// Interval since last heartbeat in milliseconds.
  int64_t interval{};

  void to_json(json &j) const;
  void from_json(const json &j);
};

/**
 * \if CHINESE
 * @brief 异常事件结构
 * \endif
 * \if ENGLISH
 * @brief Error event structure
 * \endif
 */
struct ErrorEvent {
  std::string error_type;
  std::string error_message;
  std::string target_id; // user ID or group ID
  bool is_group;
  std::chrono::system_clock::time_point time;
  json context;

  void to_json(json &j) const;
  void from_json(const json &j);
};

/**
 * \if CHINESE
 * @brief 事件变体类型
 * \endif
 * \if ENGLISH
 * @brief Event variant type
 * \endif
 */
using Event = std::variant<MessageEvent, NoticeEvent, RequestEvent, MetaEvent,
                           HeartbeatEvent, ErrorEvent>;

/**
 * \if CHINESE
 * @brief 连接配置
 * \endif
 * \if ENGLISH
 * @brief Connection configuration
 * \endif
 */
struct ConnectionConfig {
  std::string host = "localhost";
  uint16_t port = 8080;
  std::string access_token;
  std::string secret;
  std::chrono::milliseconds connect_timeout{5000};
  std::chrono::milliseconds action_timeout{30000};
  std::chrono::milliseconds poll_timeout{
      25000}; // Long-poll timeout sent to server (e.g., Telegram getUpdates)
  std::chrono::milliseconds poll_force_close{30000};
  std::chrono::milliseconds poll_retry_interval{3000};
  std::chrono::milliseconds heartbeat_interval{30000};
  bool use_ssl = false;

  // Proxy configuration
  std::string proxy_host;
  uint16_t proxy_port = 0;
  std::string proxy_type = "http"; // "http", "https", "socks5"
  std::string proxy_username;
  std::string proxy_password;
};
} // namespace obcx::common
