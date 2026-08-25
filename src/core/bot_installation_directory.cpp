#include "core/bot_installation_directory.hpp"

#include "core/bot_installation_assembler.hpp"

#include <stdexcept>
#include <utility>

namespace obcx::core {
namespace {

auto actor_surface(const common::BotInstallationSurface surface)
    -> bot::BotSurface {
  switch (surface) {
  case common::BotInstallationSurface::OneBot11Qq:
    return bot::BotSurface::OneBot11Qq;
  case common::BotInstallationSurface::TelegramBotApi:
    return bot::BotSurface::TelegramBotApi;
  }
  throw BotComponentRuntimeError("unsupported installation surface");
}

} // namespace

void BotInstallationDirectory::register_installation(
    const BotInstallation &installation) {
  auto command_catalog = std::shared_ptr<TelegramCommandCatalog>{};
  if (installation.surface() ==
      common::BotInstallationSurface::TelegramBotApi) {
    command_catalog =
        installation.capability<TelegramCommandCatalog>(CapabilityId{
            std::string{bot_capability_ids::telegram_command_catalog}});
  }
  register_capabilities(
      {.installation_id = installation.installation_id(),
       .surface = actor_surface(installation.surface())},
      installation.capability<BotOperationEndpoint>(
          CapabilityId{std::string{bot_capability_ids::operations}}),
      std::move(command_catalog));
}

void BotInstallationDirectory::register_capabilities(
    bot::BotInstallationRef installation,
    std::shared_ptr<BotOperationEndpoint> endpoint,
    std::shared_ptr<TelegramCommandCatalog> command_catalog) {
  installation.validate();
  if (endpoint == nullptr || endpoint->installation() != installation) {
    throw std::invalid_argument(
        "installation directory requires a matching operation endpoint");
  }
  if (installation.surface != bot::BotSurface::TelegramBotApi &&
      command_catalog != nullptr) {
    throw std::invalid_argument(
        "only Telegram installations can publish command catalogs");
  }
  Entry entry{.installation = std::move(installation),
              .endpoint = std::move(endpoint),
              .command_catalog = std::move(command_catalog)};
  std::scoped_lock lock(mutex_);
  if (!entries_.emplace(entry.installation.installation_id, std::move(entry))
           .second) {
    throw std::invalid_argument("duplicate bot installation id");
  }
}

void BotInstallationDirectory::unregister_installation(
    const std::string &installation_id) noexcept {
  std::scoped_lock lock(mutex_);
  entries_.erase(installation_id);
}

auto BotInstallationDirectory::endpoint(
    const bot::BotInstallationRef &installation) const
    -> std::shared_ptr<BotOperationEndpoint> {
  std::scoped_lock lock(mutex_);
  const auto entry = entries_.find(installation.installation_id);
  if (entry == entries_.end() || entry->second.installation != installation) {
    return {};
  }
  return entry->second.endpoint.lock();
}

auto BotInstallationDirectory::telegram_command_catalog(
    const bot::BotInstallationRef &installation) const
    -> std::shared_ptr<TelegramCommandCatalog> {
  if (installation.surface != bot::BotSurface::TelegramBotApi) {
    return {};
  }
  std::scoped_lock lock(mutex_);
  const auto entry = entries_.find(installation.installation_id);
  if (entry == entries_.end() || entry->second.installation != installation) {
    return {};
  }
  return entry->second.command_catalog.lock();
}

} // namespace obcx::core
