# Bot Installation Component Runtime

OBCX process runtime models each configured bot as a `BotInstallation`. An
installation owns one exact id, surface, executor, capability registry,
components, and lifecycle. It is not a universal bot object and does not
inherit messaging or provider interfaces.

Actor packages do not see this runtime. Their only bot egress service is the
installed, data-only `BotOperationClient` API.

## Reviewed recipes

Configuration selects one closed recipe:

| Surface | Transport | Components |
| --- | --- | --- |
| `onebot11.qq` | `websocket` | protocol, WebSocket transport, event ingress, operations |
| `onebot11.qq` | `http` | protocol, HTTP transport, event ingress, operations |
| `telegram.bot_api` | `http` | protocol, HTTP transport, event ingress, media upload, operations, command catalog |

Telegram WebSocket, official QQ, aliases such as `ws`, and unknown combinations
are rejected before an installation or socket is created. Users cannot provide
arbitrary component class names.

Components declare stable ids plus provided and required capability ids.
Assembly rejects duplicate providers, missing dependencies, undeclared
capabilities, and cycles. Independent components retain recipe order.
Capabilities are typed, installation-scoped process values; the registry is
not installed in `ActorServices`.

## Lifecycle

An installation performs these phases:

1. construct all components without provider I/O;
2. install capabilities and validate the dependency DAG;
3. prepare callbacks and ingress subscriptions in topological order;
4. start transports and other components in topological order;
5. close admission and stop in reverse order;
6. cancel or drain provider work before destroying components;
7. destroy the installation executor last.

Prepare/start failure rolls back every prepared component in reverse order.
Stop is idempotent. Message and notice subscribers attach to `bot.events`
before `start()`, so a transport cannot publish before actor ingress is ready.
Ingress carries the owning installation id and exact surface as data.

Operation components implement `BotOperationEndpoint` directly. The shared
process `BotOperationDispatcher` registers those endpoint capabilities by exact
installation id and surface. Supported actions come from the endpoint and the
same installation's optional capabilities; no RTTI or provider-object lookup
is used. The closed 13-action matrix remains documented in
[qq-telegram-bot-operations.md](qq-telegram-bot-operations.md).

## Canonical configuration

```toml
[bots.qq_main]
enabled = true
surface = "onebot11.qq"
transport = "websocket"

[bots.qq_main.connection]
host = "127.0.0.1"
port = 3001
access_token = "${ONEBOT_ACCESS_TOKEN}"
connect_timeout_ms = 5000
action_timeout_ms = 30000

[bots.telegram_main]
enabled = true
surface = "telegram.bot_api"
transport = "http"

[bots.telegram_main.connection]
host = "api.telegram.org"
port = 443
access_token = "${TELEGRAM_BOT_TOKEN}"
bot_username = "example_bot"
use_tls = true
poll_timeout_ms = 25000
poll_force_close_ms = 30000
poll_retry_interval_ms = 3000
```

The parser is closed per variant. Unknown, misplaced, ignored, and legacy keys
are errors with a configuration path. Telegram credentials are required;
credentials and proxy passwords are never included in diagnostics, logs, or
operator summaries.

### Legacy migration table

| Legacy field/value | Canonical replacement |
| --- | --- |
| `bots.<id>.type = "qq"` | `surface = "onebot11.qq"` plus explicit `transport` |
| `bots.<id>.type = "telegram"` | `surface = "telegram.bot_api"` and `transport = "http"` |
| `connection.type = "websocket"` | bot-level `transport = "websocket"` |
| `connection.type = "http"` | bot-level `transport = "http"` |
| `use_ssl` | `use_tls` where supported |
| `connect_timeout` | `connect_timeout_ms` |
| `action_timeout` | `action_timeout_ms` |
| `poll_timeout` | `poll_timeout_ms` |
| `poll_force_close` | `poll_force_close_ms` |
| `poll_retry_interval` | `poll_retry_interval_ms` |
| bot-level `plugins` | remove; actors and pipelines own behavior |
| generic `timeout`, `secret`, ignored heartbeat keys | remove or select an explicit supported field |

Deploy the migrated configuration and new binary together. Validate first with
`obcx --validate-config <path>`. Binary rollback also requires restoring the
previous configuration format; there is no automatic dual-schema fallback.

Credential-shaped values previously present in development configuration must
be treated as exposed: replace them with placeholders, rotate the real
OneBot/Telegram credentials outside the repository, and remove sensitive
history according to the repository's incident procedure.
