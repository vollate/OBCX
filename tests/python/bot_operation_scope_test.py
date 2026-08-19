from __future__ import annotations

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]

APPROVED_ACTIONS = frozenset(
    {
        "message.send_group",
        "message.delete",
        "telegram.message.send_topic",
        "telegram.message.edit_text",
        "telegram.media.send_photo",
        "telegram.media.send_group_urls",
        "telegram.media.send_group_uploads",
        "telegram.media.fetch_file",
        "onebot11.group_member.get",
        "onebot11.forward_message.get",
        "onebot11.group_file.resolve",
        "onebot11.private_file.resolve",
        "onebot11.group.poke",
    }
)

# Current actor calls that are folded into the finite operation contract. Several
# legacy Telegram methods intentionally become one authenticated fetch action.
LEGACY_METHOD_ACTIONS = {
    "send_group_message": "message.send_group",
    "error_notify": "message.send_group",
    "delete_message": "message.delete",
    "send_topic_message": "telegram.message.send_topic",
    "edit_message_text": "telegram.message.edit_text",
    "send_group_photo": "telegram.media.send_photo",
    "send_group_photo_with_entities": "telegram.media.send_photo",
    "send_media_group": "telegram.media.send_group_urls",
    "send_media_group_with_entities": "telegram.media.send_group_urls",
    "send_media_group_uploads": "telegram.media.send_group_uploads",
    "send_media_group_uploads_with_entities": (
        "telegram.media.send_group_uploads"
    ),
    "get_media_download_url": "telegram.media.fetch_file",
    "get_media_download_urls": "telegram.media.fetch_file",
    "download_file_content": "telegram.media.fetch_file",
    "get_group_member_info": "onebot11.group_member.get",
    "get_forward_msg": "onebot11.forward_message.get",
    "get_group_file_url": "onebot11.group_file.resolve",
    "get_private_file_url": "onebot11.private_file.resolve",
    "group_poke": "onebot11.group.poke",
}

INTERFACE_HEADERS = (
    ROOT / "include" / "interfaces" / "bot.hpp",
    ROOT / "include" / "interfaces" / "qq_bot.hpp",
    ROOT / "include" / "interfaces" / "telegram_bot.hpp",
)

PRODUCTION_ROOTS = (
    ROOT / "local_actor" / "obcx-actor-bridge" / "actor",
    ROOT / "local_actor" / "obcx-actor-bridge" / "dependency",
    ROOT / "local_actor" / "obcx-actor-bridge" / "include",
    ROOT / "local_actor" / "chat_llm",
)


def production_sources() -> list[Path]:
    excluded = {".git", "build", "docs", "tests"}
    sources: list[Path] = []
    for root in PRODUCTION_ROOTS:
        for path in root.rglob("*"):
            if not path.is_file() or path.suffix not in {".cc", ".cpp", ".h", ".hpp"}:
                continue
            if any(part in excluded or part.startswith("build-") for part in path.parts):
                continue
            sources.append(path)
    return sorted(set(sources))


def bot_interface_methods() -> set[str]:
    methods: set[str] = set()
    declaration = re.compile(r"\bvirtual\s+auto\s+(\w+)\s*\(", re.MULTILINE)
    for path in INTERFACE_HEADERS:
        methods.update(declaration.findall(path.read_text(encoding="utf-8")))
    return methods


def actor_bot_calls() -> list[tuple[Path, int, str]]:
    interface_methods = bot_interface_methods()
    method_alternation = "|".join(sorted(map(re.escape, interface_methods)))
    # Production variables carrying a bot/provider capability consistently use
    # bot, telegram, qq, or uploader in their names. Keep the receiver in the
    # match so similarly named repository methods are not mistaken for bot I/O.
    member_call = re.compile(
        rf"\b(?:\w*bot\w*|telegram\w*|qq\w*|uploader)\s*"
        rf"(?:->|\.)\s*({method_alternation})\s*\("
    )
    cast_call = re.compile(
        rf"dynamic_cast<[^>]*(?:Bot|bot)[^>]*>[^;]*?\.\s*"
        rf"({method_alternation})\s*\("
    )

    calls: list[tuple[Path, int, str]] = []
    for path in production_sources():
        content = path.read_text(encoding="utf-8")
        matches = list(member_call.finditer(content)) + list(cast_call.finditer(content))
        for match in matches:
            line = content.count("\n", 0, match.start()) + 1
            calls.append((path, line, match.group(1)))
    return sorted(set(calls), key=lambda item: (str(item[0]), item[1], item[2]))


def declared_contract_actions() -> set[str]:
    candidates = (
        ROOT / "include" / "core" / "bot_operations.hpp",
        ROOT / "include" / "core" / "bot_operation_types.hpp",
    )
    action = re.compile(
        r'"((?:message|telegram|onebot11)\.[a-z0-9_.]+)"'
    )
    declared: set[str] = set()
    for path in candidates:
        if path.exists():
            declared.update(action.findall(path.read_text(encoding="utf-8")))
    return declared - {"telegram.bot_api", "onebot11.qq"}


class BotOperationScopeTest(unittest.TestCase):
    def test_actor_bot_calls_map_only_to_the_approved_matrix(self) -> None:
        calls = actor_bot_calls()
        unknown = [
            f"{path.relative_to(ROOT)}:{line}: {method}"
            for path, line, method in calls
            if method not in LEGACY_METHOD_ACTIONS
        ]
        self.assertEqual(
            unknown,
            [],
            "Actor bot call is outside the approved QQ/Telegram matrix:\n"
            + "\n".join(unknown),
        )

        observed = {
            LEGACY_METHOD_ACTIONS[method]
            for _, _, method in calls
            if method in LEGACY_METHOD_ACTIONS
        }
        observed.update(declared_contract_actions())
        self.assertEqual(
            observed,
            APPROVED_ACTIONS,
            "Update the OpenSpec before changing the finite bot-operation matrix",
        )

    def test_legacy_mapping_itself_cannot_grow_the_matrix(self) -> None:
        self.assertEqual(set(LEGACY_METHOD_ACTIONS.values()), APPROVED_ACTIONS)


if __name__ == "__main__":
    unittest.main()
