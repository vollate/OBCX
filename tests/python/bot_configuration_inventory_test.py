from __future__ import annotations

import json
from pathlib import Path
import re
import subprocess
import tomllib
import unittest


ROOT = Path(__file__).resolve().parents[2]
INVENTORY = ROOT / "tests" / "fixtures" / "bot_configuration_inventory.json"
REPOSITORIES = (
    ROOT,
    ROOT / "local_actor" / "chat_llm",
    ROOT / "local_actor" / "obcx-actor-bridge",
    ROOT / "local_actor" / "obcx-actor-message-store",
    ROOT / "local_actor" / "obcx-actor-registry",
    ROOT / "local_actor" / "obcx-actor-template",
)
CREDENTIAL_KEYS = frozenset(
    {"access_token", "token", "secret", "proxy_password", "password"}
)
LEGACY_BOT_KEYS = frozenset({"type", "plugins"})
LEGACY_CONNECTION_KEYS = frozenset(
    {
        "type",
        "timeout",
        "connect_timeout",
        "action_timeout",
        "poll_timeout",
        "poll_force_close",
        "poll_retry_interval",
        "heartbeat_interval",
        "use_ssl",
    }
)
TOKEN_SHAPED = re.compile(r"^\d{6,}:[A-Za-z0-9_-]{20,}$")


def tracked_toml_files() -> list[tuple[Path, str]]:
    tracked: list[tuple[Path, str]] = []
    for repository in REPOSITORIES:
        if not repository.exists():
            continue
        completed = subprocess.run(
            ["git", "-C", str(repository), "ls-files", "--", "*.toml"],
            check=True,
            capture_output=True,
            text=True,
        )
        repository_name = (
            "." if repository == ROOT else repository.relative_to(ROOT).as_posix()
        )
        for relative in completed.stdout.splitlines():
            tracked.append((repository / relative, f"{repository_name}/{relative}"))
    return sorted(set(tracked), key=lambda item: item[1])


def bot_configuration_inventory() -> dict[str, dict[str, list[str]]]:
    inventory: dict[str, dict[str, list[str]]] = {}
    for path, display_path in tracked_toml_files():
        with path.open("rb") as stream:
            document = tomllib.load(stream)
        bots = document.get("bots")
        if not isinstance(bots, dict):
            continue
        legacy: list[str] = []
        credentials: list[str] = []
        for installation_id, bot in sorted(bots.items()):
            if not isinstance(bot, dict):
                continue
            prefix = f"bots.{installation_id}"
            legacy.extend(
                f"{prefix}.{key}" for key in sorted(LEGACY_BOT_KEYS & bot.keys())
            )
            connection = bot.get("connection", {})
            if isinstance(connection, dict):
                legacy.extend(
                    f"{prefix}.connection.{key}"
                    for key in sorted(LEGACY_CONNECTION_KEYS & connection.keys())
                )
                credentials.extend(
                    f"{prefix}.connection.{key}"
                    for key in sorted(CREDENTIAL_KEYS & connection.keys())
                )
        inventory[display_path] = {
            "credentials": sorted(credentials),
            "legacy": sorted(legacy),
        }
    return dict(sorted(inventory.items()))


def unsafe_credential_paths() -> list[str]:
    unsafe: list[str] = []
    for path, display_path in tracked_toml_files():
        with path.open("rb") as stream:
            document = tomllib.load(stream)
        bots = document.get("bots")
        if not isinstance(bots, dict):
            continue
        for installation_id, bot in bots.items():
            if not isinstance(bot, dict) or not isinstance(
                bot.get("connection"), dict
            ):
                continue
            for key in CREDENTIAL_KEYS & bot["connection"].keys():
                value = bot["connection"][key]
                if not isinstance(value, str) or not value:
                    continue
                lowered = value.lower()
                placeholder = (
                    "your_" in lowered
                    or "placeholder" in lowered
                    or "redacted" in lowered
                    or lowered in {"changeme", "..."}
                    or (value.startswith("${") and value.endswith("}"))
                    or (value.startswith("<") and value.endswith(">"))
                )
                if TOKEN_SHAPED.fullmatch(value) or not placeholder:
                    unsafe.append(
                        f"{display_path}:bots.{installation_id}.connection.{key}"
                    )
    return sorted(unsafe)


class BotConfigurationInventoryTest(unittest.TestCase):
    def test_tracked_bot_configuration_inventory_is_reviewed(self) -> None:
        expected = json.loads(INVENTORY.read_text(encoding="utf-8"))
        self.assertEqual(
            bot_configuration_inventory(),
            expected,
            "Tracked bot TOML schema inventory changed. Review paths only; "
            "never add credential values to the fixture or diagnostic.",
        )

    def test_tracked_bot_credentials_are_placeholders(self) -> None:
        self.assertEqual(
            unsafe_credential_paths(),
            [],
            "Tracked credential fields must be empty, environment-backed, or "
            "explicit placeholders. Values are intentionally omitted.",
        )


if __name__ == "__main__":
    unittest.main()
