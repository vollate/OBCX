#include "core/bot/messaging_client.hpp"
#include "support/sdk_gateway_fixture.hpp"
#include <gtest/gtest.h>

TEST(CommonInstalledGateway, InvokesWithoutAnyPlatformHeadersOrRuntime) {
  const obcx::bot::GroupTarget target{
      {"fixture", obcx::bot::SurfaceId{"test.echo"}}, "group"};
  const obcx::bot::SendMessageResult expected{{{target, "42"}}};
  obcx::tests::ReplyGateway gateway{
      obcx::bot::SendGroupMessageRequest::action,
      obcx::bot::OperationReply::success(obcx::bot::Json(expected))};
  const auto result = obcx::tests::await_sdk(
      obcx::bot::invoke(gateway, obcx::bot::SendGroupMessageRequest{
                                     target, {{"text", {{"text", "hello"}}}}}));
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(*result.value, expected);
  ASSERT_TRUE(gateway.observed);
  EXPECT_EQ(gateway.observed->installation, target.installation);
}
