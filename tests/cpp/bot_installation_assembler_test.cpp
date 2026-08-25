#include "core/bot_command_catalog_component.hpp"
#include "core/bot_event_components.hpp"
#include "core/bot_installation_assembler.hpp"
#include "core/bot_operation_dispatcher.hpp"
#include "core/bot_protocol_components.hpp"
#include "core/bot_transport_components.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using obcx::bot::BotAction;
using obcx::common::BotInstallationConfig;
using obcx::common::BotInstallationSurface;
using obcx::common::BotTransport;
using obcx::common::OneBot11HttpConnectionConfig;
using obcx::common::OneBot11WebSocketConnectionConfig;
using obcx::common::TelegramHttpConnectionConfig;
using obcx::core::BotComponentRuntimeError;
using obcx::core::BotInstallationAssembler;
using obcx::core::BotInstallationRecipeDescriptor;

auto onebot_websocket_config() -> BotInstallationConfig {
  return {.installation_id = "qq-ws",
          .enabled = true,
          .surface = BotInstallationSurface::OneBot11Qq,
          .transport = BotTransport::WebSocket,
          .connection = OneBot11WebSocketConnectionConfig{}};
}

auto onebot_http_config() -> BotInstallationConfig {
  return {.installation_id = "qq-http",
          .enabled = true,
          .surface = BotInstallationSurface::OneBot11Qq,
          .transport = BotTransport::Http,
          .connection = OneBot11HttpConnectionConfig{}};
}

auto telegram_http_config() -> BotInstallationConfig {
  return {.installation_id = "telegram-http",
          .enabled = true,
          .surface = BotInstallationSurface::TelegramBotApi,
          .transport = BotTransport::Http,
          .connection = TelegramHttpConnectionConfig{
              .access_token = "YOUR_TELEGRAM_TOKEN"}};
}

auto component_ids(const BotInstallationRecipeDescriptor &recipe)
    -> std::vector<std::string> {
  std::vector<std::string> ids;
  for (const auto &component : recipe.components) {
    ids.push_back(component.id.value());
  }
  return ids;
}

auto provided_capabilities(const BotInstallationRecipeDescriptor &recipe)
    -> std::set<std::string> {
  std::set<std::string> capabilities;
  for (const auto &component : recipe.components) {
    for (const auto &capability : component.provides) {
      capabilities.emplace(capability.value());
    }
  }
  return capabilities;
}

TEST(BotInstallationAssemblerTest,
     ConcreteProtocolComponentsPublishProviderTypedCapabilities) {
  obcx::core::BotInstallation onebot{"onebot-protocol-test",
                                     BotInstallationSurface::OneBot11Qq};
  onebot.add_component(
      std::make_unique<obcx::core::OneBot11ProtocolComponent>());
  onebot.assemble();
  const auto onebot_protocol =
      onebot.capability<obcx::adapter::onebot11::ProtocolAdapter>(
          obcx::core::CapabilityId{"onebot11.protocol"});
  ASSERT_NE(onebot_protocol, nullptr);
  const auto onebot_payload = onebot_protocol->serialize_send_message_request(
      "123", {{.type = "text", .data = {{"text", "hello"}}}});
  EXPECT_TRUE(nlohmann::json::parse(onebot_payload).contains("action"));

  obcx::core::BotInstallation telegram{"telegram-protocol-test",
                                       BotInstallationSurface::TelegramBotApi};
  telegram.add_component(
      std::make_unique<obcx::core::TelegramProtocolComponent>());
  telegram.assemble();
  const auto telegram_protocol =
      telegram.capability<obcx::adapter::telegram::ProtocolAdapter>(
          obcx::core::CapabilityId{"telegram.protocol"});
  ASSERT_NE(telegram_protocol, nullptr);
  const auto telegram_payload =
      telegram_protocol->serialize_send_message_request(
          "-1001", {{.type = "text", .data = {{"text", "hello"}}}});
  EXPECT_EQ(nlohmann::json::parse(telegram_payload).at("method"),
            "sendMessage");
}

TEST(BotInstallationAssemblerTest,
     TransportComponentsOwnProviderStateWithoutStartingDuringAssembly) {
  obcx::core::BotInstallation onebot_websocket{
      "onebot-websocket-test", BotInstallationSurface::OneBot11Qq};
  onebot_websocket.add_component(
      std::make_unique<obcx::core::OneBot11ProtocolComponent>());
  onebot_websocket.add_component(
      std::make_unique<obcx::core::OneBot11WebSocketTransportComponent>(
          onebot_websocket.executor(), OneBot11WebSocketConnectionConfig{}));
  onebot_websocket.assemble();
  const auto websocket =
      onebot_websocket.capability<obcx::core::OneBot11Transport>(
          obcx::core::CapabilityId{"onebot11.transport"});
  ASSERT_NE(websocket, nullptr);
  EXPECT_FALSE(websocket->is_connected());

  obcx::core::BotInstallation onebot_http{"onebot-http-test",
                                          BotInstallationSurface::OneBot11Qq};
  onebot_http.add_component(
      std::make_unique<obcx::core::OneBot11ProtocolComponent>());
  onebot_http.add_component(
      std::make_unique<obcx::core::OneBot11HttpTransportComponent>(
          onebot_http.executor(), OneBot11HttpConnectionConfig{}));
  onebot_http.assemble();
  EXPECT_FALSE(onebot_http
                   .capability<obcx::core::OneBot11Transport>(
                       obcx::core::CapabilityId{"onebot11.transport"})
                   ->is_connected());

  obcx::core::BotInstallation telegram{"telegram-http-test",
                                       BotInstallationSurface::TelegramBotApi};
  telegram.add_component(
      std::make_unique<obcx::core::TelegramProtocolComponent>());
  telegram.add_component(
      std::make_unique<obcx::core::TelegramHttpTransportComponent>(
          telegram.executor(),
          TelegramHttpConnectionConfig{.access_token = "YOUR_TELEGRAM_TOKEN"}));
  telegram.assemble();
  EXPECT_FALSE(telegram
                   .capability<obcx::core::TelegramTransport>(
                       obcx::core::CapabilityId{"telegram.transport"})
                   ->is_connected());
}

TEST(BotInstallationAssemblerTest,
     AssemblesReviewedComponentsWithoutProviderIoBeforeStart) {
  for (const auto &config : {onebot_websocket_config(), onebot_http_config(),
                             telegram_http_config()}) {
    auto installation = BotInstallationAssembler::assemble(config);
    ASSERT_NE(installation, nullptr);
    EXPECT_EQ(installation->state(),
              obcx::core::BotInstallationState::Assembled);
    EXPECT_EQ(installation->installation_id(), config.installation_id);
    EXPECT_NE(installation->capability<obcx::core::BotOperationEndpoint>(
                  obcx::core::CapabilityId{"bot.operations"}),
              nullptr);
    EXPECT_NE(installation->capability<obcx::core::BotEventCapability>(
                  obcx::core::CapabilityId{"bot.events"}),
              nullptr);
    if (config.surface == BotInstallationSurface::TelegramBotApi) {
      EXPECT_NE(installation->capability<obcx::core::TelegramCommandCatalog>(
                    obcx::core::CapabilityId{"telegram.command-catalog"}),
                nullptr);
    }
  }
}

TEST(BotInstallationAssemblerTest,
     ReviewedRecipesStartAndStopWithoutRunningProviderExecutor) {
  for (const auto &config : {onebot_websocket_config(), onebot_http_config(),
                             telegram_http_config()}) {
    auto installation = BotInstallationAssembler::assemble(config);
    const auto events =
        installation->capability<obcx::core::BotEventCapability>(
            obcx::core::CapabilityId{"bot.events"});
    events->subscribe_messages(
        [](const obcx::core::BotEventContext &,
           const obcx::common::MessageEvent &) -> boost::asio::awaitable<void> {
          co_return;
        });
    installation->start();
    EXPECT_EQ(installation->state(), obcx::core::BotInstallationState::Running);
    EXPECT_TRUE(installation->accepting_work());
    installation->stop();
    installation->stop();
    EXPECT_EQ(installation->state(), obcx::core::BotInstallationState::Stopped);
    EXPECT_FALSE(installation->accepting_work());
  }
}

TEST(BotInstallationAssemblerTest, SelectsExactReviewedRecipes) {
  const auto websocket =
      BotInstallationAssembler::describe(onebot_websocket_config());
  EXPECT_EQ(websocket.recipe_id, "onebot11.qq.websocket");
  EXPECT_EQ(component_ids(websocket),
            (std::vector<std::string>{
                "onebot11.protocol", "onebot11.transport.websocket",
                "onebot11.event-ingress", "onebot11.operations"}));

  const auto onebot_http =
      BotInstallationAssembler::describe(onebot_http_config());
  EXPECT_EQ(onebot_http.recipe_id, "onebot11.qq.http");
  EXPECT_EQ(component_ids(onebot_http),
            (std::vector<std::string>{
                "onebot11.protocol", "onebot11.transport.http",
                "onebot11.event-ingress", "onebot11.operations"}));

  const auto telegram =
      BotInstallationAssembler::describe(telegram_http_config());
  EXPECT_EQ(telegram.recipe_id, "telegram.bot_api.http");
  EXPECT_EQ(component_ids(telegram),
            (std::vector<std::string>{
                "telegram.protocol", "telegram.transport.http",
                "telegram.event-ingress", "telegram.media-upload",
                "telegram.operations", "telegram.command-catalog"}));
}

TEST(BotInstallationAssemblerTest,
     RecipesPublishOnlyTheirReviewedCapabilitySets) {
  const auto websocket =
      BotInstallationAssembler::describe(onebot_websocket_config());
  EXPECT_EQ(provided_capabilities(websocket),
            (std::set<std::string>{"bot.events", "bot.operations",
                                   "onebot11.protocol", "onebot11.transport"}));
  EXPECT_EQ(
      std::set<BotAction>(websocket.advertised_actions.begin(),
                          websocket.advertised_actions.end()),
      (std::set<BotAction>{
          BotAction::SendGroupMessage, BotAction::DeleteMessage,
          BotAction::GetOneBotGroupMember, BotAction::GetOneBotForwardMessage,
          BotAction::ResolveOneBotGroupFile,
          BotAction::ResolveOneBotPrivateFile, BotAction::PokeOneBotGroup}));

  const auto telegram =
      BotInstallationAssembler::describe(telegram_http_config());
  EXPECT_EQ(provided_capabilities(telegram),
            (std::set<std::string>{"bot.events", "bot.operations",
                                   "telegram.command-catalog",
                                   "telegram.media-upload", "telegram.protocol",
                                   "telegram.transport"}));
  EXPECT_EQ(telegram.advertised_actions.size(), 8U);
}

TEST(BotInstallationAssemblerTest,
     DescriptorValidationIsDeterministicAndSideEffectFree) {
  EXPECT_EQ(BotInstallationAssembler::validate(onebot_websocket_config())
                .lifecycle_order,
            (std::vector<std::size_t>{0, 1, 2, 3}));
  EXPECT_EQ(
      BotInstallationAssembler::validate(onebot_http_config()).lifecycle_order,
      (std::vector<std::size_t>{0, 1, 2, 3}));
  EXPECT_EQ(BotInstallationAssembler::validate(telegram_http_config())
                .lifecycle_order,
            (std::vector<std::size_t>{0, 1, 2, 3, 4, 5}));
}

TEST(BotInstallationAssemblerTest,
     OptionalTelegramMediaCapabilityCanBeOmittedFromATestRecipe) {
  auto telegram = BotInstallationAssembler::describe(telegram_http_config());
  const auto media = std::ranges::find(
      telegram.components, std::string_view{"telegram.media-upload"},
      [](const auto &component) {
        return std::string_view{component.id.value()};
      });
  ASSERT_NE(media, telegram.components.end());
  telegram.components.erase(media);
  EXPECT_NO_THROW(
      (void)obcx::core::validate_component_recipe(telegram.components));
  EXPECT_FALSE(
      provided_capabilities(telegram).contains("telegram.media-upload"));
}

TEST(BotInstallationAssemblerTest,
     RejectsMismatchedVariantsAndUnsupportedTelegramWebSocket) {
  auto mismatched = telegram_http_config();
  mismatched.connection = OneBot11HttpConnectionConfig{};
  EXPECT_THROW((void)BotInstallationAssembler::describe(mismatched),
               BotComponentRuntimeError);

  auto telegram_websocket = onebot_websocket_config();
  telegram_websocket.surface = BotInstallationSurface::TelegramBotApi;
  EXPECT_THROW((void)BotInstallationAssembler::describe(telegram_websocket),
               BotComponentRuntimeError);
}

} // namespace
