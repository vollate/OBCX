#include "core/actor_messages.hpp"
#include "core/command_coordinator.hpp"
#include "core/reflected_actor.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

namespace obcx::tests::command_runtime {

struct TestCommand final : obcx::command::RequestMessage<TestCommand> {};

class CommandActor final : public obcx::core::ReflectedActor<CommandActor> {
public:
  static constexpr std::string_view actor_name = "command_actor";
  static constexpr std::string_view actor_version = "1.0.0";

  static constexpr auto command_contract() {
    return obcx::command::catalog(obcx::command::observe<TestCommand>(
        "test", "Run a command coordinator test"));
  }

  auto handle(const TestCommand &request,
              const obcx::core::MessageEnvelope &message,
              obcx::core::ActorContext &context)
      -> obcx::core::ActorTask<obcx::core::ActorResult> {
    ++command_count;
    if (request.invocation.arguments == "timeout") {
      for (;;) {
        co_await context.yield();
      }
    }
    if (request.invocation.arguments == "failure") {
      co_return obcx::core::ActorResult::failed(
          "test_actor_failure", "test command actor failed", true);
    }

    auto result = obcx::core::ActorResult::success();
    obcx::core::MessageEnvelope business;
    business.id = message.id + ":business";
    business.type = "CommandBusiness";
    business.source_platform = message.source_platform;
    business.source_bot = message.source_bot;
    business.conversation_id = message.conversation_id;
    business.headers = message.headers;
    business.payload = {
        {"command", request.invocation.name},
        {"arguments", request.invocation.arguments},
    };
    result.emit(std::move(business));

    if (request.invocation.arguments == "missing") {
      co_return result;
    }
    const auto propagation = request.invocation.arguments == "continue"
                                 ? obcx::command::Propagation::Continue
                                 : obcx::command::Propagation::Consume;
    result.emit(
        obcx::command::CommandCompleted{
            .transaction_id = request.invocation.transaction_id,
            .propagation = propagation,
        },
        message);
    if (request.invocation.arguments == "duplicate") {
      result.emit(
          obcx::command::CommandCompleted{
              .transaction_id = request.invocation.transaction_id,
              .propagation = obcx::command::Propagation::Consume,
          },
          message);
    } else if (request.invocation.arguments == "wrong_generation") {
      result.emitted.back().headers.insert_or_assign(
          std::string{obcx::core::command_generation_header}, "999");
    } else if (request.invocation.arguments == "malformed") {
      result.emitted.back().payload = "invalid";
    }
    co_return result;
  }

  auto handle(const obcx::core::events::RawMessageEvent &,
              const obcx::core::MessageEnvelope &message,
              obcx::core::ActorContext &) -> obcx::core::ActorResult {
    ++raw_count;
    auto result = obcx::core::ActorResult::success();
    obcx::core::MessageEnvelope observed;
    observed.id = message.id + ":raw";
    observed.type = "RawObserved";
    observed.headers = message.headers;
    result.emit(std::move(observed));
    return result;
  }

  std::atomic_int command_count = 0;
  std::atomic_int raw_count = 0;
};

} // namespace obcx::tests::command_runtime

namespace {

namespace asio = boost::asio;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

template <typename T>
auto run_awaitable(asio::io_context &ioc, asio::awaitable<T> awaitable) -> T {
  std::optional<T> result;
  std::exception_ptr exception;
  asio::co_spawn(
      ioc,
      [&]() -> asio::awaitable<void> {
        try {
          result = co_await std::move(awaitable);
        } catch (...) {
          exception = std::current_exception();
        }
      },
      asio::detached);
  ioc.run();
  ioc.restart();
  if (exception) {
    std::rethrow_exception(exception);
  }
  return std::move(*result);
}

auto command_contract(const bool accept_request = true,
                      std::optional<std::string> pattern = std::nullopt)
    -> obcx::core::ActorInputContract {
  const auto request_type = std::string{obcx::core::canonical_message_type_name<
      obcx::tests::command_runtime::TestCommand>()};
  auto contract = obcx::core::ActorInputContract{
      .schema_version = 1,
      .actor = "command_actor",
      .accepted_inputs =
          {
              std::string{obcx::core::canonical_message_type_name<
                  obcx::core::events::RawMessageEvent>()},
          },
      .accepted_input_set =
          {
              std::string{obcx::core::canonical_message_type_name<
                  obcx::core::events::RawMessageEvent>()},
          },
      .commands =
          {
              {.name = "test",
               .description = "Run a command coordinator test",
               .request_type = request_type},
          },
  };
  if (accept_request) {
    contract.accepted_inputs.push_back(request_type);
    contract.accepted_input_set.insert(request_type);
  }
  if (pattern) {
    contract.commands.front().matcher = obcx::core::ActorCommandMatcher{
        .kind = "re2",
        .pattern = std::move(*pattern),
        .mode = "full",
    };
  }
  return contract;
}

auto config_document(std::string fallback = "continue",
                     std::string platform = "qq", std::string bot_type = "qq",
                     std::string actor = "command_actor",
                     std::string command = "test") -> std::string {
  const auto telegram = bot_type == "telegram";
  const auto surface =
      std::string{telegram ? "telegram.bot_api" : "onebot11.qq"};
  const auto connection =
      std::string{telegram ? "access_token = \"YOUR_TELEGRAM_TOKEN\"\n\n"
                           : "access_token = \"\"\n\n"};
  return "[bots.primary]\n"
         "enabled = true\n"
         "surface = \"" +
         surface +
         "\"\n"
         "transport = \"http\"\n\n"
         "[bots.primary.connection]\n" +
         connection +
         "[actors.command_actor]\n"
         "enabled = true\n"
         "partition = \"conversation_id\"\n\n"
         "[command_runtime]\n"
         "timeout_ms = 100\n\n"
         "[[command_runtime.routes]]\n"
         "actor = \"" +
         actor +
         "\"\n"
         "commands = [\"" +
         command +
         "\"]\n"
         "platforms = [\"" +
         platform +
         "\"]\n"
         "bots = [\"primary\"]\n"
         "fallback = \"" +
         fallback + "\"\n";
}

class CommandCoordinatorTest : public ::testing::Test {
protected:
  void SetUp() override {
    root_ = fs::temp_directory_path() /
            ("obcx-command-coordinator-" +
             std::to_string(
                 std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root_);
  }

  void TearDown() override { fs::remove_all(root_); }

  auto snapshot(const std::string &name, const std::string &document)
      -> std::shared_ptr<const obcx::common::RuntimeConfigSnapshot> {
    const auto path = root_ / name;
    {
      std::ofstream output(path);
      output << document;
    }
    const auto built = obcx::common::ConfigLoader::build_snapshot(path);
    EXPECT_TRUE(built);
    return built.snapshot;
  }

  auto table(
      const std::shared_ptr<const obcx::common::RuntimeConfigSnapshot> &config,
      const bool accept_request = true)
      -> obcx::core::CommandRoutingBuildResult {
    return obcx::core::build_command_routing_table(
        *config, {{"command_actor", command_contract(accept_request)}});
  }

  auto raw(std::string arguments) -> obcx::core::MessageEnvelope {
    obcx::core::MessageEnvelope message;
    message.id = "raw-" + arguments;
    message.type = obcx::core::canonical_message_type_name<
        obcx::core::events::RawMessageEvent>();
    message.source_platform = "qq";
    message.source_bot = "primary";
    message.conversation_id = "group:42";
    message.payload = {{"sender", "7"}};
    message.raw = {{"raw_message", "/test " + arguments}};
    return message;
  }

  auto raw_command(std::string command, std::string arguments)
      -> obcx::core::MessageEnvelope {
    auto message = raw(std::move(arguments));
    message.raw["raw_message"] = "/" + std::move(command) + " " +
                                 message.id.substr(std::string{"raw-"}.size());
    return message;
  }

  struct Runtime {
    std::shared_ptr<obcx::core::ActorServices> services;
    std::shared_ptr<obcx::core::NativeActorScheduler> scheduler;
    std::shared_ptr<obcx::core::Orchestrator> orchestrator;
    std::shared_ptr<obcx::tests::command_runtime::CommandActor> actor;
    std::shared_ptr<obcx::core::CommandCoordinator> coordinator;

    ~Runtime() {
      coordinator->shutdown();
      orchestrator->shutdown();
    }
  };

  auto runtime(
      const std::shared_ptr<const obcx::common::RuntimeConfigSnapshot> &config,
      std::shared_ptr<const obcx::core::CommandRoutingTable> routing_table = {})
      -> Runtime {
    if (!routing_table) {
      auto built_table = table(config);
      EXPECT_TRUE(built_table);
      routing_table = std::move(built_table.table);
    }
    auto services = std::make_shared<obcx::core::ActorServices>();
    auto scheduler = std::make_shared<obcx::core::NativeActorScheduler>(
        obcx::core::NativeActorSchedulerOptions{.worker_count = 2}, services);
    auto orchestrator =
        std::make_shared<obcx::core::Orchestrator>(scheduler, services);
    auto actor = std::make_shared<obcx::tests::command_runtime::CommandActor>();
    orchestrator->register_actor(actor);
    orchestrator->configure_actors(config->get_actor_configs());
    orchestrator->configure_pipelines(
        {{.name = "raw",
          .source = std::string{obcx::core::canonical_message_type_name<
              obcx::core::events::RawMessageEvent>()},
          .stages = {
              {.name = "observe",
               .actor = "command_actor",
               .input = std::string{obcx::core::canonical_message_type_name<
                   obcx::core::events::RawMessageEvent>()},
               .outputs = {"RawObserved"},
               .mode = "await"}}}});
    auto coordinator = std::make_shared<obcx::core::CommandCoordinator>(
        7, std::move(routing_table), scheduler, orchestrator);
    return Runtime{
        .services = std::move(services),
        .scheduler = std::move(scheduler),
        .orchestrator = std::move(orchestrator),
        .actor = std::move(actor),
        .coordinator = std::move(coordinator),
    };
  }

  fs::path root_;
};

TEST_F(CommandCoordinatorTest, BuildsImmutableRoutesAndDetectionOnlyCatalogs) {
  const auto config = snapshot("valid.toml", config_document());
  const auto built = table(config);
  ASSERT_TRUE(built) << (built.failure ? built.failure->message : "");
  ASSERT_EQ(built.table->routes().size(), 1U);
  ASSERT_EQ(built.table->bots().size(), 1U);
  const auto &route = built.table->routes().begin()->second;
  EXPECT_EQ(route.actor, "command_actor");
  EXPECT_EQ(route.request_type,
            obcx::core::canonical_message_type_name<
                obcx::tests::command_runtime::TestCommand>());
  EXPECT_EQ(route.timeout, 100ms);
  const auto &bot = built.table->bots().begin()->second;
  ASSERT_EQ(bot.catalog.size(), 1U);
  EXPECT_EQ(bot.catalog.front().name, "test");
  EXPECT_FALSE(bot.adapter->supports_catalog_publication());
}

TEST_F(CommandCoordinatorTest,
       UsesConfiguredTelegramUsernameForExplicitCommandTargets) {
  auto document = config_document("continue", "telegram", "telegram");
  const auto connection = document.find("[bots.primary.connection]\n");
  ASSERT_NE(connection, std::string::npos);
  document.insert(connection +
                      std::string{"[bots.primary.connection]\n"}.size(),
                  "bot_username = \"my_bot\"\n");
  const auto config = snapshot("telegram-target.toml", document);
  const auto built = table(config);
  ASSERT_TRUE(built) << (built.failure ? built.failure->message : "");
  ASSERT_EQ(built.table->bots().size(), 1U);
  const auto &bot = built.table->bots().begin()->second;
  EXPECT_EQ(bot.target, "my_bot");

  obcx::core::MessageEnvelope event;
  event.type = obcx::core::canonical_message_type_name<
      obcx::core::events::RawMessageEvent>();
  event.source_platform = "telegram";
  event.source_bot = "primary";
  event.raw = {
      {"text", "/test@my_bot"},
      {"entities",
       obcx::common::json::array(
           {{{"type", "bot_command"}, {"offset", 0}, {"length", 12}}})},
  };
  const auto detected = bot.adapter->detect(event, bot.target);
  ASSERT_TRUE(detected.has_value());
  EXPECT_EQ(detected->name, "test");
  EXPECT_FALSE(bot.adapter->detect(event, "other_bot").has_value());
}

TEST_F(CommandCoordinatorTest, AggregatesCommandsFromMultipleActorsPerBot) {
  auto document = config_document();
  document += "\n[actors.other_actor]\n"
              "enabled = true\n\n"
              "[[command_runtime.routes]]\n"
              "actor = \"other_actor\"\n"
              "commands = [\"other\"]\n"
              "platforms = [\"qq\"]\n"
              "bots = [\"primary\"]\n"
              "fallback = \"consume\"\n";
  const auto config = snapshot("aggregate.toml", document);
  auto other = command_contract();
  other.actor = "other_actor";
  other.commands.front().name = "other";
  other.commands.front().description = "Run another actor command";
  const auto built = obcx::core::build_command_routing_table(
      *config, {{"command_actor", command_contract()}, {"other_actor", other}});
  ASSERT_TRUE(built) << (built.failure ? built.failure->message : "");
  ASSERT_EQ(built.table->routes().size(), 2U);
  ASSERT_EQ(built.table->bots().size(), 1U);
  const auto &catalog = built.table->bots().begin()->second.catalog;
  ASSERT_EQ(catalog.size(), 2U);
  EXPECT_EQ(catalog[0].name, "other");
  EXPECT_EQ(catalog[1].name, "test");
}

TEST_F(CommandCoordinatorTest, RejectsInvalidActorCommandBotAndAdapterEdges) {
  const auto missing_actor = snapshot(
      "missing-actor.toml", config_document("continue", "qq", "qq", "missing"));
  auto built = table(missing_actor);
  ASSERT_TRUE(built.failure);
  EXPECT_EQ(built.failure->code, "command_actor_unavailable");

  const auto missing_command = snapshot(
      "missing-command.toml",
      config_document("continue", "qq", "qq", "command_actor", "missing"));
  built = table(missing_command);
  ASSERT_TRUE(built.failure);
  EXPECT_EQ(built.failure->code, "command_not_declared");

  const auto unsupported = snapshot("unsupported.toml", config_document());
  built = table(unsupported, false);
  ASSERT_TRUE(built.failure);
  EXPECT_EQ(built.failure->code, "command_request_unsupported");

  const auto mismatched = snapshot(
      "mismatched.toml", config_document("continue", "telegram", "qq"));
  built = table(mismatched);
  ASSERT_TRUE(built.failure);
  EXPECT_EQ(built.failure->code, "command_bot_platform_mismatch");

  const auto unavailable = snapshot(
      "unavailable.toml", config_document("continue", "matrix", "matrix"));
  built = table(unavailable);
  ASSERT_TRUE(built.failure);
  EXPECT_EQ(built.failure->code, "command_platform_adapter_unavailable");
}

TEST_F(CommandCoordinatorTest, RejectsScopedRouteConflictsBeforeActivation) {
  auto document = config_document();
  document += "\n[[command_runtime.routes]]\n"
              "actor = \"command_actor\"\n"
              "commands = [\"test\"]\n"
              "platforms = [\"qq\"]\n"
              "bots = [\"primary\"]\n"
              "fallback = \"consume\"\n";
  const auto config = snapshot("conflict.toml", document);
  const auto built = table(config);
  ASSERT_TRUE(built.failure);
  EXPECT_EQ(built.failure->code, "command_route_conflict");
}

TEST_F(CommandCoordinatorTest,
       RoutesOneFullMatchWithCanonicalIdentityAndUnchangedArguments) {
  const auto config = snapshot("pattern.toml", config_document());
  const auto built = obcx::core::build_command_routing_table(
      *config,
      {{"command_actor", command_contract(true, R"(^(?:test|alias)$)")}});
  ASSERT_TRUE(built) << (built.failure ? built.failure->message : "");
  const auto &bot = built.table->bots().begin()->second;
  ASSERT_EQ(bot.patterns.size(), 1U);
  EXPECT_EQ(bot.catalog.size(), 1U);
  EXPECT_EQ(bot.catalog.front().name, "test");

  auto active = runtime(config, built.table);
  asio::io_context ioc;
  const auto result = run_awaitable(
      ioc, active.coordinator->process(raw_command("alias", "continue"),
                                       std::make_shared<int>(8)));
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(active.actor->command_count, 1);
  EXPECT_EQ(active.actor->raw_count, 1);
  const auto business =
      std::ranges::find(result.emitted, std::string{"CommandBusiness"},
                        &obcx::core::MessageEnvelope::type);
  ASSERT_NE(business, result.emitted.end());
  EXPECT_EQ(business->payload.at("command"), "test");
  EXPECT_EQ(business->payload.at("arguments"), "continue");
  const auto observed =
      std::ranges::find(result.emitted, std::string{"RawObserved"},
                        &obcx::core::MessageEnvelope::type);
  ASSERT_NE(observed, result.emitted.end());
  EXPECT_EQ(observed->headers.at(std::string{obcx::core::command_name_header}),
            "test");

  const auto unmatched = run_awaitable(
      ioc, active.coordinator->process(raw_command("prefixalias", "consume"),
                                       std::make_shared<int>(9)));
  EXPECT_TRUE(unmatched.ok());
  EXPECT_EQ(active.actor->command_count, 1);
  EXPECT_EQ(active.actor->raw_count, 2);
}

TEST_F(CommandCoordinatorTest,
       ExactCanonicalRouteWinsWithoutEvaluatingOverlappingPatterns) {
  auto document = config_document("consume");
  document += "\n[actors.other_actor]\n"
              "enabled = true\n\n"
              "[[command_runtime.routes]]\n"
              "actor = \"other_actor\"\n"
              "commands = [\"other\"]\n"
              "platforms = [\"qq\"]\n"
              "bots = [\"primary\"]\n"
              "fallback = \"consume\"\n";
  const auto config = snapshot("exact-precedence.toml", document);
  auto other = command_contract(true, R"(^test$)");
  other.actor = "other_actor";
  other.commands.front().name = "other";
  const auto built = obcx::core::build_command_routing_table(
      *config, {{"command_actor", command_contract(true, R"(^alias$)")},
                {"other_actor", std::move(other)}});
  ASSERT_TRUE(built) << (built.failure ? built.failure->message : "");

  auto active = runtime(config, built.table);
  asio::io_context ioc;
  const auto result = run_awaitable(
      ioc, active.coordinator->process(raw_command("test", "consume"),
                                       std::make_shared<int>(10)));
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(active.actor->command_count, 1);
  EXPECT_EQ(active.actor->raw_count, 0);
}

TEST_F(CommandCoordinatorTest,
       RejectsIdenticalPatternsAndContinuesAmbiguousMessagesExactlyOnce) {
  auto document = config_document("consume");
  document += "\n[actors.other_actor]\n"
              "enabled = true\n\n"
              "[[command_runtime.routes]]\n"
              "actor = \"other_actor\"\n"
              "commands = [\"other\"]\n"
              "platforms = [\"qq\"]\n"
              "bots = [\"primary\"]\n"
              "fallback = \"consume\"\n";
  const auto config = snapshot("pattern-conflicts.toml", document);
  auto other = command_contract(true, R"(^alias$)");
  other.actor = "other_actor";
  other.commands.front().name = "other";

  auto built = obcx::core::build_command_routing_table(
      *config, {{"command_actor", command_contract(true, R"(^alias$)")},
                {"other_actor", other}});
  ASSERT_TRUE(built.failure);
  EXPECT_EQ(built.failure->code, "command_pattern_conflict");

  other.commands.front().matcher->pattern = R"(^(?:alias|other_alias)$)";
  built = obcx::core::build_command_routing_table(
      *config, {{"command_actor", command_contract(true, R"(^alias$)")},
                {"other_actor", std::move(other)}});
  ASSERT_TRUE(built) << (built.failure ? built.failure->message : "");

  auto active = runtime(config, built.table);
  asio::io_context ioc;
  const auto result = run_awaitable(
      ioc, active.coordinator->process(raw_command("alias", "consume"),
                                       std::make_shared<int>(11)));
  ASSERT_EQ(result.failures.size(), 1U);
  EXPECT_EQ(result.failures.front().failure.code, "command_match_ambiguous");
  EXPECT_EQ(active.actor->command_count, 0);
  EXPECT_EQ(active.actor->raw_count, 1);
  ASSERT_EQ(result.emitted.size(), 1U);
  EXPECT_EQ(result.emitted.front().type, "RawObserved");
  EXPECT_FALSE(result.emitted.front().headers.contains(
      std::string{obcx::core::command_processed_header}));
}

TEST_F(CommandCoordinatorTest,
       RoutesTypedCompletionAndAppliesContinueOrConsumeOnce) {
  const auto config = snapshot("routing.toml", config_document());
  auto active = runtime(config);
  asio::io_context ioc;

  auto continued =
      run_awaitable(ioc, active.coordinator->process(raw("continue"),
                                                     std::make_shared<int>(1)));
  EXPECT_TRUE(continued.ok());
  EXPECT_EQ(active.actor->command_count, 1);
  EXPECT_EQ(active.actor->raw_count, 1);
  ASSERT_EQ(continued.emitted.size(), 2U);
  const auto observed =
      std::ranges::find(continued.emitted, std::string{"RawObserved"},
                        &obcx::core::MessageEnvelope::type);
  ASSERT_NE(observed, continued.emitted.end());
  EXPECT_EQ(
      observed->headers.at(std::string{obcx::core::command_processed_header}),
      "true");
  EXPECT_EQ(
      observed->headers.at(std::string{obcx::core::command_outcome_header}),
      "continue");
  EXPECT_EQ(
      observed->headers.at(std::string{obcx::core::command_generation_header}),
      "7");

  auto consumed =
      run_awaitable(ioc, active.coordinator->process(raw("consume"),
                                                     std::make_shared<int>(2)));
  EXPECT_TRUE(consumed.ok());
  EXPECT_EQ(active.actor->command_count, 2);
  EXPECT_EQ(active.actor->raw_count, 1);
  ASSERT_EQ(consumed.emitted.size(), 1U);
  EXPECT_EQ(consumed.emitted.front().type, "CommandBusiness");

  auto unmatched = raw("consume");
  unmatched.raw["raw_message"] = "/unknown consume";
  auto passed =
      run_awaitable(ioc, active.coordinator->process(std::move(unmatched),
                                                     std::make_shared<int>(3)));
  EXPECT_TRUE(passed.ok());
  EXPECT_EQ(active.actor->command_count, 2);
  EXPECT_EQ(active.actor->raw_count, 2);

  auto processed = raw("consume");
  processed.headers.emplace(std::string{obcx::core::command_processed_header},
                            "true");
  passed =
      run_awaitable(ioc, active.coordinator->process(std::move(processed),
                                                     std::make_shared<int>(4)));
  EXPECT_TRUE(passed.ok());
  EXPECT_EQ(active.actor->command_count, 2);
  EXPECT_EQ(active.actor->raw_count, 3);
}

TEST_F(CommandCoordinatorTest,
       RejectsMissingDuplicateMalformedAndWrongGenerationCompletions) {
  const auto config = snapshot("fallback.toml", config_document());
  auto active = runtime(config);
  asio::io_context ioc;

  const std::array cases = {
      std::pair{"missing", "command_completion_missing"},
      std::pair{"duplicate", "command_completion_duplicate"},
      std::pair{"malformed", "command_completion_malformed"},
      std::pair{"wrong_generation", "command_completion_mismatch"},
      std::pair{"failure", "command_actor_failure"},
  };
  for (const auto &[argument, code] : cases) {
    const auto result = run_awaitable(
        ioc,
        active.coordinator->process(raw(argument), std::make_shared<int>(5)));
    ASSERT_EQ(result.failures.size(), 1U);
    EXPECT_EQ(result.failures.front().failure.code, code);
  }
  EXPECT_EQ(active.actor->raw_count, cases.size());
}

TEST_F(CommandCoordinatorTest, TimesOutCooperativeActorAndUsesFallback) {
  const auto config = snapshot("timeout.toml", config_document());
  auto active = runtime(config);
  asio::io_context ioc;
  const auto started = std::chrono::steady_clock::now();
  const auto result =
      run_awaitable(ioc, active.coordinator->process(raw("timeout"),
                                                     std::make_shared<int>(6)));
  const auto elapsed = std::chrono::steady_clock::now() - started;
  ASSERT_EQ(result.failures.size(), 1U);
  EXPECT_EQ(result.failures.front().failure.code, "command_timeout");
  EXPECT_EQ(active.actor->raw_count, 1);
  EXPECT_LT(elapsed, 2s);
}

TEST_F(CommandCoordinatorTest,
       ShutdownCancelsPendingCommandAndCompletesSourceOnce) {
  auto document = config_document("consume");
  document.replace(document.find("timeout_ms = 100"),
                   std::string{"timeout_ms = 100"}.size(), "timeout_ms = 5000");
  const auto config = snapshot("shutdown.toml", document);
  auto active = runtime(config);
  asio::io_context ioc;
  auto completed = asio::co_spawn(
      ioc,
      active.coordinator->process(raw("timeout"), std::make_shared<int>(7)),
      asio::use_future);
  std::jthread io_thread([&] { ioc.run(); });

  const auto deadline = std::chrono::steady_clock::now() + 2s;
  while (active.actor->command_count == 0 &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(1ms);
  }
  ASSERT_EQ(active.actor->command_count, 1);

  active.coordinator->shutdown();
  active.orchestrator->shutdown();
  ASSERT_EQ(completed.wait_for(2s), std::future_status::ready);
  const auto result = completed.get();
  ASSERT_EQ(result.failures.size(), 1U);
  EXPECT_EQ(result.failures.front().failure.code, "command_actor_failure");
  EXPECT_EQ(active.actor->raw_count, 0);
}

} // namespace
