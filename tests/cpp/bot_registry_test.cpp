#include "core/bot_registry.hpp"
#include "core/qq_bot.hpp"
#include "onebot11/adapter/protocol_adapter.hpp"

#include <gtest/gtest.h>

namespace obcx::core {
namespace {

TEST(BotRegistryTest, PreservesMultipleAccountsOnOnePlatform) {
  auto primary = std::make_shared<QQBot>(adapter::onebot11::ProtocolAdapter{});
  auto secondary =
      std::make_shared<QQBot>(adapter::onebot11::ProtocolAdapter{});
  BotRegistry registry;

  registry.register_bot("qq", "primary", primary);
  registry.register_bot("qq", "secondary", secondary);

  const auto primary_registration = registry.find_bot("qq", "primary");
  ASSERT_TRUE(primary_registration.has_value());
  EXPECT_EQ(primary_registration->bot, primary);
  const auto secondary_registration = registry.find_bot("qq", "secondary");
  ASSERT_TRUE(secondary_registration.has_value());
  EXPECT_EQ(secondary_registration->bot, secondary);
  EXPECT_FALSE(registry.find_bot("qq").has_value());
}

TEST(BotRegistryTest, PlatformLookupWorksForOneUnambiguousAccount) {
  auto primary = std::make_shared<QQBot>(adapter::onebot11::ProtocolAdapter{});
  BotRegistry registry;
  registry.register_bot("qq", "primary", primary);

  const auto registration = registry.find_bot("qq");
  ASSERT_TRUE(registration.has_value());
  EXPECT_EQ(registration->bot_id, "primary");
  EXPECT_EQ(registration->bot, primary);
}

TEST(BotRegistryTest, ExpiredAndUnregisteredBotsAreNotReturned) {
  BotRegistry registry;
  auto expired = std::make_shared<QQBot>(adapter::onebot11::ProtocolAdapter{});
  registry.register_bot("qq", "expired", expired);
  expired.reset();
  EXPECT_FALSE(registry.find_bot("qq", "expired").has_value());

  auto removed = std::make_shared<QQBot>(adapter::onebot11::ProtocolAdapter{});
  registry.register_bot("qq", "removed", removed);
  registry.unregister_bot("qq", "removed");
  EXPECT_FALSE(registry.find_bot("qq", "removed").has_value());
}

} // namespace
} // namespace obcx::core
