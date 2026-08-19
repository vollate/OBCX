#include "core/qq_bot.hpp"
#include "core/tg_bot.hpp"
#include "interfaces/connection_manager.hpp"
#include "onebot11/adapter/protocol_adapter.hpp"
#include "telegram/adapter/protocol_adapter.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <deque>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace asio = boost::asio;

namespace {

struct ScriptedOutcome {
  std::string response;
  std::string exception;
};

class ScriptedConnectionManager final
    : public obcx::network::IConnectionManager {
public:
  void connect(const obcx::common::ConnectionConfig &) override {
    connected_ = true;
  }

  void disconnect() override { connected_ = false; }

  [[nodiscard]] auto is_connected() const -> bool override {
    return connected_;
  }

  auto send_action_and_wait_async(std::string action_payload,
                                  std::uint64_t echo_id)
      -> asio::awaitable<std::string> override {
    payloads.push_back(std::move(action_payload));
    echo_ids.push_back(echo_id);
    if (outcomes.empty()) {
      throw std::runtime_error("scripted connection has no outcome");
    }
    auto outcome = std::move(outcomes.front());
    outcomes.pop_front();
    if (!outcome.exception.empty()) {
      throw std::runtime_error(outcome.exception);
    }
    co_return std::move(outcome.response);
  }

  void set_event_callback(EventCallback callback) override {
    callback_ = std::move(callback);
  }

  [[nodiscard]] auto get_connection_type() const -> std::string override {
    return "scripted";
  }

  void push_response(std::string response) {
    outcomes.push_back({.response = std::move(response)});
  }

  void push_exception(std::string message) {
    outcomes.push_back({.exception = std::move(message)});
  }

  std::deque<ScriptedOutcome> outcomes;
  std::vector<std::string> payloads;
  std::vector<std::uint64_t> echo_ids;

private:
  bool connected_{true};
  EventCallback callback_;
};

class BaselineQQBot final : public obcx::core::QQBot {
public:
  BaselineQQBot() : QQBot(obcx::adapter::onebot11::ProtocolAdapter{}) {}

  auto install(std::unique_ptr<ScriptedConnectionManager> manager)
      -> ScriptedConnectionManager * {
    auto *observed = manager.get();
    connection_manager_ = std::move(manager);
    return observed;
  }
};

class BaselineTelegramBot final : public obcx::core::TGBot {
public:
  BaselineTelegramBot() : TGBot(obcx::adapter::telegram::ProtocolAdapter{}) {}

  auto install(std::unique_ptr<ScriptedConnectionManager> manager)
      -> ScriptedConnectionManager * {
    auto *observed = manager.get();
    connection_manager_ = std::move(manager);
    return observed;
  }
};

template <typename T> auto run(asio::awaitable<T> operation) -> T {
  asio::io_context context;
  auto future = asio::co_spawn(context, std::move(operation), asio::use_future);
  context.run();
  return future.get();
}

auto text_message(std::string text) -> obcx::common::Message {
  return {{.type = "text", .data = {{"text", std::move(text)}}}};
}

TEST(BotTransportBaselineTest, OneBotGroupSendReturnsProviderBodiesUnchanged) {
  BaselineQQBot bot;
  auto manager = std::make_unique<ScriptedConnectionManager>();
  manager->push_response(
      R"({"status":"ok","retcode":0,"data":{"message_id":18}})");
  manager->push_response(
      R"({"status":"failed","retcode":1200,"message":"denied"})");
  manager->push_response("not-json");
  auto *observed = bot.install(std::move(manager));

  const auto success =
      run(bot.send_group_message("qq-group", text_message("a")));
  const auto rejected =
      run(bot.send_group_message("qq-group", text_message("b")));
  const auto malformed =
      run(bot.send_group_message("qq-group", text_message("c")));

  EXPECT_EQ(success, R"({"status":"ok","retcode":0,"data":{"message_id":18}})");
  EXPECT_EQ(rejected,
            R"({"status":"failed","retcode":1200,"message":"denied"})");
  EXPECT_EQ(malformed, "not-json");
  ASSERT_EQ(observed->payloads.size(), 3U);
  const auto payload = nlohmann::json::parse(observed->payloads.front());
  EXPECT_EQ(payload.at("action"), "send_group_msg");
  EXPECT_EQ(payload.at("params").at("group_id"), "qq-group");
}

TEST(BotTransportBaselineTest,
     TelegramGroupSendReturnsProviderBodiesUnchanged) {
  BaselineTelegramBot bot;
  auto manager = std::make_unique<ScriptedConnectionManager>();
  manager->push_response(R"({"ok":true,"result":{"message_id":17}})");
  manager->push_response(
      R"({"ok":false,"error_code":403,"description":"forbidden"})");
  manager->push_response("{");
  auto *observed = bot.install(std::move(manager));

  const auto success =
      run(bot.send_group_message("tg-chat", text_message("a")));
  const auto rejected =
      run(bot.send_group_message("tg-chat", text_message("b")));
  const auto malformed =
      run(bot.send_group_message("tg-chat", text_message("c")));

  EXPECT_EQ(success, R"({"ok":true,"result":{"message_id":17}})");
  EXPECT_EQ(rejected,
            R"({"ok":false,"error_code":403,"description":"forbidden"})");
  EXPECT_EQ(malformed, "{");
  ASSERT_EQ(observed->payloads.size(), 3U);
  const auto payload = nlohmann::json::parse(observed->payloads.front());
  EXPECT_EQ(payload.at("method"), "sendMessage");
  EXPECT_EQ(payload.at("chat_id"), "tg-chat");
}

TEST(BotTransportBaselineTest, OneBotDeleteReturnsSuccessAndFailureUnchanged) {
  BaselineQQBot bot;
  auto manager = std::make_unique<ScriptedConnectionManager>();
  manager->push_response(R"({"status":"ok","retcode":0,"data":null})");
  manager->push_response(
      R"({"status":"failed","retcode":1404,"message":"missing"})");
  auto *observed = bot.install(std::move(manager));

  EXPECT_EQ(run(bot.delete_message("qq-message")),
            R"({"status":"ok","retcode":0,"data":null})");
  EXPECT_EQ(run(bot.delete_message("qq-message")),
            R"({"status":"failed","retcode":1404,"message":"missing"})");
  const auto payload = nlohmann::json::parse(observed->payloads.front());
  EXPECT_EQ(payload.at("action"), "delete_msg");
  EXPECT_EQ(payload.at("params").at("message_id"), "qq-message");
}

TEST(BotTransportBaselineTest,
     TelegramDeleteSplitsLegacyChatAndMessageAndReturnsBodiesUnchanged) {
  BaselineTelegramBot bot;
  auto manager = std::make_unique<ScriptedConnectionManager>();
  manager->push_response(R"({"ok":true,"result":true})");
  manager->push_response(
      R"({"ok":false,"error_code":400,"description":"not found"})");
  auto *observed = bot.install(std::move(manager));

  EXPECT_EQ(run(bot.delete_message("-1001:42")),
            R"({"ok":true,"result":true})");
  EXPECT_EQ(run(bot.delete_message("-1001:42")),
            R"({"ok":false,"error_code":400,"description":"not found"})");
  const auto payload = nlohmann::json::parse(observed->payloads.front());
  EXPECT_EQ(payload.at("method"), "deleteMessage");
  EXPECT_EQ(payload.at("chat_id"), "-1001");
  EXPECT_EQ(payload.at("message_id"), "42");
}

TEST(BotTransportBaselineTest, SideEffectExceptionsPropagateFromBothBots) {
  BaselineQQBot qq;
  auto qq_manager = std::make_unique<ScriptedConnectionManager>();
  qq_manager->push_exception("onebot write outcome unknown");
  qq.install(std::move(qq_manager));

  BaselineTelegramBot telegram;
  auto telegram_manager = std::make_unique<ScriptedConnectionManager>();
  telegram_manager->push_exception("telegram write outcome unknown");
  telegram.install(std::move(telegram_manager));

  EXPECT_THROW(
      {
        try {
          (void)run(qq.send_group_message("qq-group", text_message("x")));
        } catch (const std::runtime_error &error) {
          EXPECT_STREQ(error.what(), "onebot write outcome unknown");
          throw;
        }
      },
      std::runtime_error);
  EXPECT_THROW(
      {
        try {
          (void)run(telegram.send_group_message("tg-chat", text_message("x")));
        } catch (const std::runtime_error &error) {
          EXPECT_STREQ(error.what(), "telegram write outcome unknown");
          throw;
        }
      },
      std::runtime_error);
}

} // namespace
