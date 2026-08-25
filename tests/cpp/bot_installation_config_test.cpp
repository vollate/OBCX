#include "common/config_loader.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>
#include <vector>

namespace {

namespace fs = std::filesystem;
using obcx::common::BotInstallationConfig;
using obcx::common::BotInstallationSurface;
using obcx::common::BotProxyType;
using obcx::common::BotTransport;
using obcx::common::ConfigLoader;
using obcx::common::OneBot11HttpConnectionConfig;
using obcx::common::OneBot11WebSocketConnectionConfig;
using obcx::common::RuntimeConfigBuildResult;
using obcx::common::TelegramHttpConnectionConfig;

class BotInstallationConfigTest : public ::testing::Test {
protected:
  void SetUp() override {
    const auto *test_info =
        ::testing::UnitTest::GetInstance()->current_test_info();
    ASSERT_NE(test_info, nullptr);
    root_ = fs::temp_directory_path() / "obcx_bot_installation_config_test" /
            test_info->name();
    fs::remove_all(root_);
    fs::create_directories(root_);
  }

  void TearDown() override { fs::remove_all(root_); }

  auto parse(std::string_view document) -> RuntimeConfigBuildResult {
    const auto path = root_ / "config.toml";
    std::ofstream output(path);
    output << document;
    output.close();
    return ConfigLoader::build_snapshot(path.string());
  }

  fs::path root_;
};

auto find_installation(const std::vector<BotInstallationConfig> &configs,
                       const std::string_view id)
    -> const BotInstallationConfig * {
  const auto found =
      std::ranges::find(configs, id, &BotInstallationConfig::installation_id);
  return found == configs.end() ? nullptr : &*found;
}

TEST_F(BotInstallationConfigTest, ParsesAllSupportedTypedVariants) {
  const auto built = parse(R"(
[bots.qq_ws]
enabled = true
surface = "onebot11.qq"
transport = "websocket"

[bots.qq_ws.connection]
host = "127.0.0.1"
port = 3001
access_token = ""
connect_timeout_ms = 4000
action_timeout_ms = 25000

[bots.qq_http]
enabled = false
surface = "onebot11.qq"
transport = "http"

[bots.qq_http.connection]
host = "onebot.internal"
port = 8443
use_tls = true
poll_interval_ms = 750

[bots.telegram]
enabled = true
surface = "telegram.bot_api"
transport = "http"

[bots.telegram.connection]
access_token = "YOUR_TELEGRAM_BOT_TOKEN"
bot_username = "fixture_bot"
proxy_host = "127.0.0.1"
proxy_port = 1080
proxy_type = "socks5"
proxy_username = "fixture"
proxy_password = "YOUR_PROXY_PASSWORD"
poll_timeout_ms = 20000
poll_force_close_ms = 25000
poll_retry_interval_ms = 1200
)");
  ASSERT_TRUE(built) << (built.diagnostic ? built.diagnostic->code : "");
  const auto configs = built.snapshot->get_bot_configs();
  ASSERT_EQ(configs.size(), 3U);

  const auto *qq_ws = find_installation(configs, "qq_ws");
  ASSERT_NE(qq_ws, nullptr);
  EXPECT_EQ(qq_ws->surface, BotInstallationSurface::OneBot11Qq);
  EXPECT_EQ(qq_ws->transport, BotTransport::WebSocket);
  const auto &websocket =
      std::get<OneBot11WebSocketConnectionConfig>(qq_ws->connection);
  EXPECT_EQ(websocket.host, "127.0.0.1");
  EXPECT_EQ(websocket.port, 3001);
  EXPECT_EQ(websocket.connect_timeout, std::chrono::milliseconds{4000});
  EXPECT_EQ(websocket.action_timeout, std::chrono::milliseconds{25000});

  const auto *qq_http = find_installation(configs, "qq_http");
  ASSERT_NE(qq_http, nullptr);
  EXPECT_FALSE(qq_http->enabled);
  const auto &onebot_http =
      std::get<OneBot11HttpConnectionConfig>(qq_http->connection);
  EXPECT_TRUE(onebot_http.use_tls);
  EXPECT_EQ(onebot_http.poll_interval, std::chrono::milliseconds{750});

  const auto *telegram = find_installation(configs, "telegram");
  ASSERT_NE(telegram, nullptr);
  EXPECT_EQ(telegram->surface, BotInstallationSurface::TelegramBotApi);
  EXPECT_EQ(telegram->transport, BotTransport::Http);
  const auto &telegram_http =
      std::get<TelegramHttpConnectionConfig>(telegram->connection);
  EXPECT_EQ(telegram_http.host, "api.telegram.org");
  EXPECT_EQ(telegram_http.port, 443);
  EXPECT_TRUE(telegram_http.use_tls);
  EXPECT_EQ(telegram_http.bot_username, "fixture_bot");
  ASSERT_TRUE(telegram_http.proxy.has_value());
  EXPECT_EQ(telegram_http.proxy->type, BotProxyType::Socks5);
  EXPECT_EQ(telegram_http.proxy->port, 1080);
}

TEST_F(BotInstallationConfigTest, AppliesVariantSpecificDefaults) {
  const auto built = parse(R"(
[bots.qq]
enabled = true
surface = "onebot11.qq"
transport = "websocket"
[bots.qq.connection]

[bots.telegram]
enabled = true
surface = "telegram.bot_api"
transport = "http"
[bots.telegram.connection]
access_token = "YOUR_TOKEN"
)");
  ASSERT_TRUE(built);
  const auto configs = built.snapshot->get_bot_configs();
  const auto *qq = find_installation(configs, "qq");
  const auto *telegram = find_installation(configs, "telegram");
  ASSERT_NE(qq, nullptr);
  ASSERT_NE(telegram, nullptr);
  const auto &websocket =
      std::get<OneBot11WebSocketConnectionConfig>(qq->connection);
  EXPECT_EQ(websocket.host, "localhost");
  EXPECT_EQ(websocket.port, 3001);
  EXPECT_EQ(websocket.connect_timeout, std::chrono::milliseconds{5000});
  const auto &http =
      std::get<TelegramHttpConnectionConfig>(telegram->connection);
  EXPECT_EQ(http.poll_timeout, std::chrono::milliseconds{25000});
  EXPECT_EQ(http.poll_force_close, std::chrono::milliseconds{30000});
  EXPECT_EQ(http.poll_retry_interval, std::chrono::milliseconds{3000});
}

TEST_F(BotInstallationConfigTest, RejectsUnknownAndLegacyKeysByExactPath) {
  for (const auto &[document, code, path] :
       std::vector<std::tuple<std::string, std::string, std::string>>{
           {R"(
[bots.qq]
type = "qq"
enabled = true
surface = "onebot11.qq"
transport = "websocket"
[bots.qq.connection]
)",
            "legacy_bot_configuration_key", "bots.qq.type"},
           {R"(
[bots.qq]
enabled = true
surface = "onebot11.qq"
transport = "websocket"
plugins = []
[bots.qq.connection]
)",
            "legacy_bot_configuration_key", "bots.qq.plugins"},
           {R"(
[bots.qq]
enabled = true
surface = "onebot11.qq"
transport = "websocket"
[bots.qq.connection]
timeout = 10
)",
            "legacy_bot_configuration_key", "bots.qq.connection.timeout"},
           {R"(
[bots.qq]
enabled = true
surface = "onebot11.qq"
transport = "websocket"
[bots.qq.connection]
misspelled_timeout_ms = 10
)",
            "unknown_bot_configuration_key",
            "bots.qq.connection.misspelled_timeout_ms"}}) {
    const auto built = parse(document);
    ASSERT_FALSE(built);
    ASSERT_TRUE(built.diagnostic.has_value());
    EXPECT_EQ(built.diagnostic->code, code);
    EXPECT_EQ(built.diagnostic->path, path);
  }
}

TEST_F(BotInstallationConfigTest, RejectsUnsupportedSurfacesAndTransports) {
  for (const auto &[document, code] :
       std::vector<std::pair<std::string, std::string>>{
           {R"(
[bots.official]
enabled = true
surface = "qq.official"
transport = "http"
[bots.official.connection]
)",
            "unsupported_bot_surface"},
           {R"(
[bots.telegram]
enabled = true
surface = "telegram.bot_api"
transport = "websocket"
[bots.telegram.connection]
access_token = "YOUR_TOKEN"
)",
            "unsupported_bot_surface_transport"},
           {R"(
[bots.qq]
enabled = true
surface = "onebot11.qq"
transport = "ws"
[bots.qq.connection]
)",
            "unsupported_bot_transport"}}) {
    const auto built = parse(document);
    ASSERT_FALSE(built);
    ASSERT_TRUE(built.diagnostic.has_value());
    EXPECT_EQ(built.diagnostic->code, code);
  }
}

TEST_F(BotInstallationConfigTest,
       RejectsInvalidCredentialsDurationsAndProxyCombinations) {
  for (const auto &document : std::vector<std::string>{
           R"(
[bots.telegram]
enabled = true
surface = "telegram.bot_api"
transport = "http"
[bots.telegram.connection]
access_token = ""
)",
           R"(
[bots.qq]
enabled = true
surface = "onebot11.qq"
transport = "http"
[bots.qq.connection]
action_timeout_ms = 0
)",
           R"(
[bots.telegram]
enabled = true
surface = "telegram.bot_api"
transport = "http"
[bots.telegram.connection]
access_token = "YOUR_TOKEN"
proxy_type = "socks5"
)",
           R"(
[bots.telegram]
enabled = true
surface = "telegram.bot_api"
transport = "http"
[bots.telegram.connection]
access_token = "YOUR_TOKEN"
poll_timeout_ms = 30000
poll_force_close_ms = 20000
)"}) {
    const auto built = parse(document);
    ASSERT_FALSE(built);
    ASSERT_TRUE(built.diagnostic.has_value());
    EXPECT_TRUE(built.diagnostic->code.starts_with("invalid_bot"));
  }
}

TEST_F(BotInstallationConfigTest, SchemaDiagnosticsNeverContainCredentials) {
  constexpr std::string_view credential =
      "123456789:THIS_VALUE_MUST_NEVER_APPEAR_IN_A_DIAGNOSTIC";
  const auto built = parse(R"(
[bots.telegram]
enabled = true
surface = "telegram.bot_api"
transport = "http"
[bots.telegram.connection]
access_token = "123456789:THIS_VALUE_MUST_NEVER_APPEAR_IN_A_DIAGNOSTIC"
ignored = true
)");
  ASSERT_FALSE(built);
  ASSERT_TRUE(built.diagnostic.has_value());
  EXPECT_EQ(built.diagnostic->code.find(credential), std::string::npos);
  EXPECT_EQ(built.diagnostic->path.find(credential), std::string::npos);
  EXPECT_EQ(built.diagnostic->message.find(credential), std::string::npos);
}

} // namespace
