from __future__ import annotations

from collections import Counter
import json
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
INVENTORY = (
    ROOT / "tests" / "fixtures" / "bot_component_legacy_inventory.json"
)
SOURCE_SUFFIXES = frozenset({".cc", ".cpp", ".h", ".hpp"})
EXCLUDED_PARTS = frozenset({".git", "build", "docs", "tests"})
PRODUCTION_ROOTS = (ROOT / "include", ROOT / "src", ROOT / "local_actor")

LEGACY_TOKENS = (
    "IBot",
    "IQQBot",
    "ITelegramBot",
    "ITelegramMediaGroupUploader",
    "BotRegistry",
    "RegisteredBot",
    "QQBot",
    "TGBot",
    "ComponentManager",
    "ConnectionManagerFactory",
)
LIVE_BOT_RTTI = re.compile(
    r"\bdynamic_(?:pointer_)?cast\s*<[^;(){}]*(?:IBot|IQQBot|"
    r"ITelegramBot|ITelegramMediaGroupUploader|QQBot|TGBot)[^;(){}]*>"
)


def production_sources() -> list[Path]:
    sources: list[Path] = []
    for root in PRODUCTION_ROOTS:
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
                continue
            relative_parts = path.relative_to(root).parts
            if any(
                part in EXCLUDED_PARTS or part.startswith("build-")
                for part in relative_parts
            ):
                continue
            sources.append(path)
    return sorted(set(sources))


def current_inventory() -> dict[str, dict[str, int]]:
    inventory: dict[str, Counter[str]] = {}
    token_patterns = {
        token: re.compile(rf"\b{re.escape(token)}\b") for token in LEGACY_TOKENS
    }
    for path in production_sources():
        content = path.read_text(encoding="utf-8")
        counts: Counter[str] = Counter()
        for token, pattern in token_patterns.items():
            counts[token] = len(pattern.findall(content))
        counts["live_bot_rtti"] = len(LIVE_BOT_RTTI.findall(content))
        counts = Counter({key: value for key, value in counts.items() if value})
        if counts:
            inventory[path.relative_to(ROOT).as_posix()] = counts
    return {
        path: dict(sorted(counts.items()))
        for path, counts in sorted(inventory.items())
    }


class BotComponentMigrationInventoryTest(unittest.TestCase):
    def test_legacy_runtime_inventory_matches_reviewed_baseline(self) -> None:
        expected = json.loads(INVENTORY.read_text(encoding="utf-8"))
        actual = current_inventory()
        self.assertEqual(
            actual,
            expected,
            "Legacy bot-runtime inventory changed. Review the diff and update "
            "the inventory as each migration task removes an entry; do not add "
            "new legacy dependencies.",
        )

    def test_dispatch_and_native_endpoints_have_no_live_bot_cast_path(self) -> None:
        paths = (
            ROOT / "src" / "core" / "bot_operation_dispatcher.cpp",
            ROOT / "src" / "core" / "bot_operation_components.cpp",
            ROOT / "include" / "core" / "bot_operation_dispatcher.hpp",
            ROOT / "include" / "core" / "bot_operation_components.hpp",
        )
        findings: list[str] = []
        for path in paths:
            content = path.read_text(encoding="utf-8")
            for pattern in (
                r"dynamic_(?:pointer_)?cast\s*<[^>]*(?:IBot|IQQBot|ITelegramBot|QQBot|TGBot)",
                r"#\s*include\s*[<\"](?:interfaces/(?:bot|qq_bot|telegram_bot)|core/(?:qq_bot|tg_bot|bot_registry))\.hpp[>\"]",
            ):
                if re.search(pattern, content):
                    findings.append(path.relative_to(ROOT).as_posix())
        self.assertEqual(findings, [])

    def test_completed_runtime_contains_no_retirement_category(self) -> None:
        self.assertEqual(current_inventory(), {})
        removed_paths = (
            ROOT / "include" / "interfaces" / "bot.hpp",
            ROOT / "include" / "interfaces" / "qq_bot.hpp",
            ROOT / "include" / "interfaces" / "telegram_bot.hpp",
            ROOT / "include" / "core" / "qq_bot.hpp",
            ROOT / "include" / "core" / "tg_bot.hpp",
            ROOT / "include" / "core" / "bot_registry.hpp",
            ROOT / "include" / "common" / "component_manager.hpp",
        )
        self.assertEqual([str(path) for path in removed_paths if path.exists()], [])


if __name__ == "__main__":
    unittest.main()
