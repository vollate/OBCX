from __future__ import annotations

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]


def reloadable_actor_sources() -> list[Path]:
    roots = (
        ROOT / "local_actor" / "obcx-actor-bridge",
        ROOT / "local_actor" / "obcx-actor-message-store",
        ROOT / "local_actor" / "obcx-actor-template",
        ROOT / "tests" / "fixtures" / "standalone_v2_actor",
    )
    sources: list[Path] = []
    for root in roots:
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if ".git" in path.parts or "tests" in path.relative_to(root).parts:
                continue
            if path.is_file() and path.suffix in {".cpp", ".hpp"}:
                sources.append(path)
    return sorted(sources)


def migrated_bot_actor_sources() -> list[Path]:
    roots = (
        ROOT / "local_actor" / "obcx-actor-bridge" / "actor",
        ROOT / "local_actor" / "obcx-actor-bridge" / "dependency",
        ROOT / "local_actor" / "obcx-actor-bridge" / "include",
        ROOT / "local_actor" / "chat_llm",
    )
    excluded = {".git", "build", "docs", "tests"}
    sources: list[Path] = []
    for root in roots:
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if not path.is_file() or path.suffix not in {
                ".cc",
                ".cpp",
                ".h",
                ".hpp",
            }:
                continue
            if any(
                part in excluded or part.startswith("build-")
                for part in path.parts
            ):
                continue
            sources.append(path)
    return sorted(set(sources))


def production_scheduler_sources() -> list[Path]:
    roots = (
        ROOT / "include",
        ROOT / "src",
        ROOT / "local_actor" / "chat_llm",
    )
    excluded = {".git", "build", "build-asan", "build-tsan", "docs", "tests"}
    sources: list[Path] = []
    for root in roots:
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if not path.is_file() or path.suffix not in {".cc", ".cpp", ".h", ".hpp"}:
                continue
            if any(part in excluded or part.startswith("build-") for part in path.parts):
                continue
            sources.append(path)
    return sorted(sources)


class ActorArchitectureTest(unittest.TestCase):
    def test_reloadable_actors_use_generation_scoped_configuration(self) -> None:
        banned = (
            re.compile(r"\bConfigLoader\s*::\s*instance\s*\("),
            re.compile(r"\bextern\s+[^;\n]*(?:GROUP_MAP|CONFIG)[^;\n]*;"),
        )
        findings: list[str] = []
        for path in reloadable_actor_sources():
            content = path.read_text(encoding="utf-8")
            for pattern in banned:
                for match in pattern.finditer(content):
                    line = content.count("\n", 0, match.start()) + 1
                    findings.append(
                        f"{path.relative_to(ROOT)}:{line}: {match.group(0)}"
                    )
        self.assertEqual(findings, [], "\n" + "\n".join(findings))

        fixture = (
            ROOT / "tests" / "fixtures" / "standalone_v2_actor" / "actor.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("context.config()", fixture)

    def test_production_sources_expose_no_retired_bot_scheduler_api(self) -> None:
        banned = (
            re.compile(r"\bTaskScheduler\b"),
            re.compile(r"\bget_task_scheduler\b"),
            re.compile(r"\brun_heavy_task\b"),
        )
        findings: list[str] = []
        for path in production_scheduler_sources():
            content = path.read_text(encoding="utf-8")
            for pattern in banned:
                for match in pattern.finditer(content):
                    line = content.count("\n", 0, match.start()) + 1
                    findings.append(
                        f"{path.relative_to(ROOT)}:{line}: {match.group(0)}"
                    )
        self.assertEqual(findings, [], "\n" + "\n".join(findings))
        self.assertFalse((ROOT / "include" / "core" / "task_scheduler.hpp").exists())

    def test_migrated_qq_telegram_actors_expose_no_live_bot_boundary(self) -> None:
        banned = (
            re.compile(
                r"#\s*include\s*[<\"](?:core/bot_registry|"
                r"interfaces/(?:bot|qq_bot|telegram_bot)|"
                r"core/(?:qq_bot|tg_bot)|[^>\"]*connection_manager)\.hpp[>\"]"
            ),
            re.compile(r"\bBotRegistry\b"),
            re.compile(r"\b(?:IBot|IQQBot|ITelegramBot)\b"),
            re.compile(
                r"\bdynamic_cast\s*<[^>]*"
                r"(?:IBot|IQQBot|ITelegramBot|QQBot|TGBot)[^>]*>"
            ),
            re.compile(r"\bfind_bot\s*\("),
            re.compile(r"\bget_service\s*<[^>]*BotRegistry[^>]*>"),
        )
        findings: list[str] = []
        seen_operation_client: set[str] = set()
        for path in migrated_bot_actor_sources():
            content = path.read_text(encoding="utf-8")
            relative = path.relative_to(ROOT)
            if "BotOperationClient" in content:
                seen_operation_client.add(relative.parts[1])
            for pattern in banned:
                for match in pattern.finditer(content):
                    line = content.count("\n", 0, match.start()) + 1
                    findings.append(f"{relative}:{line}: {match.group(0)}")
        self.assertEqual(findings, [], "\n" + "\n".join(findings))
        self.assertIn("obcx-actor-bridge", seen_operation_client)
        self.assertIn("chat_llm", seen_operation_client)

    def test_blocking_executor_completion_has_no_polling_bridge(self) -> None:
        implementation = (
            ROOT / "include" / "core" / "blocking_executor.hpp"
        ).read_text(encoding="utf-8")
        for pattern in (
            r"\bstd::future\b",
            r"\bfuture_status\b",
            r"\bsteady_timer\b",
            r"milliseconds\s*[({]\s*1\s*[)}]",
        ):
            self.assertNotRegex(implementation, pattern)


if __name__ == "__main__":
    unittest.main()
