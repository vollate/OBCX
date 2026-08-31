#include "core/bot/bot_installation_assembler.hpp"

#include "core/bot/bot_command_catalog_component.hpp"
#include "core/bot/bot_event_components.hpp"
#include "core/bot/bot_operation_components.hpp"
#include "core/bot/bot_protocol_components.hpp"
#include "core/bot/bot_transport_components.hpp"

#include <stdexcept>
#include <variant>

namespace obcx::core {
namespace {

auto onebot_websocket_recipe() -> BotInstallationRecipeDescriptor {
  using bot_capability_ids::events;
  using bot_capability_ids::onebot11_protocol;
  using bot_capability_ids::onebot11_transport;
  using bot_capability_ids::operations;
  return {
      .recipe_id = "onebot11.qq.websocket",
      .surface = common::BotInstallationSurface::OneBot11Qq,
      .transport = common::BotTransport::WebSocket,
      .components =
          {
              {.id = ComponentId{"onebot11.protocol"},
               .provides = {CapabilityId{std::string{onebot11_protocol}}},
               .required = {}},
              {.id = ComponentId{"onebot11.transport.websocket"},
               .provides = {CapabilityId{std::string{onebot11_transport}}},
               .required = {CapabilityId{std::string{onebot11_protocol}}}},
              {.id = ComponentId{"onebot11.event-ingress"},
               .provides = {CapabilityId{std::string{events}}},
               .required = {CapabilityId{std::string{onebot11_protocol}},
                            CapabilityId{std::string{onebot11_transport}}}},
              {.id = ComponentId{"onebot11.operations"},
               .provides = {CapabilityId{std::string{operations}}},
               .required = {CapabilityId{std::string{onebot11_protocol}},
                            CapabilityId{std::string{onebot11_transport}}}},
          },
      .advertised_actions =
          {
              bot::BotAction::SendGroupMessage,
              bot::BotAction::DeleteMessage,
              bot::BotAction::GetOneBotGroupMember,
              bot::BotAction::GetOneBotForwardMessage,
              bot::BotAction::ResolveOneBotGroupFile,
              bot::BotAction::ResolveOneBotPrivateFile,
              bot::BotAction::PokeOneBotGroup,
          },
  };
}

auto onebot_http_recipe() -> BotInstallationRecipeDescriptor {
  auto recipe = onebot_websocket_recipe();
  recipe.recipe_id = "onebot11.qq.http";
  recipe.transport = common::BotTransport::Http;
  recipe.components[1].id = ComponentId{"onebot11.transport.http"};
  return recipe;
}

auto telegram_http_recipe() -> BotInstallationRecipeDescriptor {
  using bot_capability_ids::events;
  using bot_capability_ids::operations;
  using bot_capability_ids::telegram_command_catalog;
  using bot_capability_ids::telegram_media_upload;
  using bot_capability_ids::telegram_protocol;
  using bot_capability_ids::telegram_transport;
  return {
      .recipe_id = "telegram.bot_api.http",
      .surface = common::BotInstallationSurface::TelegramBotApi,
      .transport = common::BotTransport::Http,
      .components =
          {
              {.id = ComponentId{"telegram.protocol"},
               .provides = {CapabilityId{std::string{telegram_protocol}}},
               .required = {}},
              {.id = ComponentId{"telegram.transport.http"},
               .provides = {CapabilityId{std::string{telegram_transport}}},
               .required = {CapabilityId{std::string{telegram_protocol}}}},
              {.id = ComponentId{"telegram.event-ingress"},
               .provides = {CapabilityId{std::string{events}}},
               .required = {CapabilityId{std::string{telegram_protocol}},
                            CapabilityId{std::string{telegram_transport}}}},
              {.id = ComponentId{"telegram.media-upload"},
               .provides = {CapabilityId{std::string{telegram_media_upload}}},
               .required = {CapabilityId{std::string{telegram_transport}}}},
              {.id = ComponentId{"telegram.operations"},
               .provides = {CapabilityId{std::string{operations}}},
               .required = {CapabilityId{std::string{telegram_protocol}},
                            CapabilityId{std::string{telegram_transport}}}},
              {.id = ComponentId{"telegram.command-catalog"},
               .provides = {CapabilityId{
                   std::string{telegram_command_catalog}}},
               .required = {CapabilityId{std::string{telegram_protocol}},
                            CapabilityId{std::string{telegram_transport}}}},
          },
      .advertised_actions =
          {
              bot::BotAction::SendGroupMessage,
              bot::BotAction::DeleteMessage,
              bot::BotAction::SendTelegramTopicMessage,
              bot::BotAction::EditTelegramMessageText,
              bot::BotAction::SendTelegramPhoto,
              bot::BotAction::SendTelegramMediaGroupUrls,
              bot::BotAction::SendTelegramMediaGroupUploads,
              bot::BotAction::FetchTelegramFile,
          },
  };
}

void validate_variant_matches_recipe(
    const common::BotInstallationConfig &config) {
  const auto matches =
      (config.surface == common::BotInstallationSurface::OneBot11Qq &&
       config.transport == common::BotTransport::WebSocket &&
       std::holds_alternative<common::OneBot11WebSocketConnectionConfig>(
           config.connection)) ||
      (config.surface == common::BotInstallationSurface::OneBot11Qq &&
       config.transport == common::BotTransport::Http &&
       std::holds_alternative<common::OneBot11HttpConnectionConfig>(
           config.connection)) ||
      (config.surface == common::BotInstallationSurface::TelegramBotApi &&
       config.transport == common::BotTransport::Http &&
       std::holds_alternative<common::TelegramHttpConnectionConfig>(
           config.connection));
  if (!matches) {
    throw BotComponentRuntimeError(
        "typed bot connection variant does not match surface/transport");
  }
}

} // namespace

auto BotInstallationAssembler::describe(
    const common::BotInstallationConfig &config)
    -> BotInstallationRecipeDescriptor {
  validate_variant_matches_recipe(config);
  if (config.surface == common::BotInstallationSurface::OneBot11Qq &&
      config.transport == common::BotTransport::WebSocket) {
    return onebot_websocket_recipe();
  }
  if (config.surface == common::BotInstallationSurface::OneBot11Qq &&
      config.transport == common::BotTransport::Http) {
    return onebot_http_recipe();
  }
  if (config.surface == common::BotInstallationSurface::TelegramBotApi &&
      config.transport == common::BotTransport::Http) {
    return telegram_http_recipe();
  }
  throw BotComponentRuntimeError(
      "unsupported bot installation surface/transport recipe");
}

auto BotInstallationAssembler::validate(
    const common::BotInstallationConfig &config) -> ComponentRecipeValidation {
  return validate_component_recipe(describe(config).components);
}

auto BotInstallationAssembler::assemble(
    const common::BotInstallationConfig &config)
    -> std::unique_ptr<BotInstallation> {
  (void)describe(config);
  auto installation =
      std::make_unique<BotInstallation>(config.installation_id, config.surface);
  if (config.surface == common::BotInstallationSurface::OneBot11Qq) {
    installation->add_component(std::make_unique<OneBot11ProtocolComponent>());
    if (config.transport == common::BotTransport::WebSocket) {
      installation->add_component(
          std::make_unique<OneBot11WebSocketTransportComponent>(
              installation->executor(),
              std::get<common::OneBot11WebSocketConnectionConfig>(
                  config.connection)));
    } else {
      installation->add_component(
          std::make_unique<OneBot11HttpTransportComponent>(
              installation->executor(),
              std::get<common::OneBot11HttpConnectionConfig>(
                  config.connection)));
    }
    installation->add_component(std::make_unique<OneBot11EventIngressComponent>(
        installation->executor().get_executor(), config.installation_id));
    installation->add_component(
        std::make_unique<OneBot11OperationsComponent>(config.installation_id));
  } else {
    installation->add_component(std::make_unique<TelegramProtocolComponent>());
    installation->add_component(
        std::make_unique<TelegramHttpTransportComponent>(
            installation->executor(),
            std::get<common::TelegramHttpConnectionConfig>(config.connection)));
    installation->add_component(std::make_unique<TelegramEventIngressComponent>(
        installation->executor().get_executor(), config.installation_id));
    installation->add_component(
        std::make_unique<TelegramMediaUploadComponent>());
    installation->add_component(
        std::make_unique<TelegramOperationsComponent>(config.installation_id));
    installation->add_component(
        std::make_unique<TelegramCommandCatalogComponent>());
  }
  installation->assemble();
  return installation;
}

} // namespace obcx::core
