from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "cmake" / "parse_actor_packages.py"
SPEC = importlib.util.spec_from_file_location("parse_actor_packages", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot import {MODULE_PATH}")
manifest = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = manifest
SPEC.loader.exec_module(manifest)


class ActorPackageManifestTest(unittest.TestCase):
    def parse(self, content: str) -> list[tuple[str, str, str, str]]:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "actors.toml"
            path.write_text(content, encoding="utf-8")
            return manifest.parse_manifest(path)

    def test_parses_local_and_pinned_remote_actor_packages(self) -> None:
        records = self.parse(
            """\
schema_version = 1

[[actors]]
path = "local_actor/message-store"

[[actors]]
repository = "https://github.com/Onebot-CXX/bridge.git"
revision = "0123456789abcdef"
enabled = false
"""
        )
        self.assertEqual(
            records,
            [
                ("LOCAL", "true", "local_actor/message-store", ""),
                (
                    "REMOTE",
                    "false",
                    "https://github.com/Onebot-CXX/bridge.git",
                    "0123456789abcdef",
                ),
            ],
        )

    def test_rejects_unpinned_remote_package(self) -> None:
        with self.assertRaisesRegex(ValueError, "revision must be a non-empty"):
            self.parse(
                """\
schema_version = 1
[[actors]]
repository = "https://github.com/Onebot-CXX/bridge.git"
"""
            )

    def test_rejects_boolean_schema_version(self) -> None:
        with self.assertRaisesRegex(ValueError, "schema_version must equal 1"):
            self.parse(
                """\
schema_version = true
actors = []
"""
            )

    def test_rejects_unknown_or_ambiguous_entries(self) -> None:
        with self.assertRaisesRegex(ValueError, "unexpected actors manifest"):
            self.parse(
                """\
schema_version = 1
[local]
packages = []
actors = []
"""
            )
        with self.assertRaisesRegex(ValueError, "exactly one"):
            self.parse(
                """\
schema_version = 1
[[actors]]
path = "local_actor/bridge"
repository = "https://github.com/Onebot-CXX/bridge.git"
revision = "v1.0.0"
"""
            )

    def test_rejects_duplicate_sources(self) -> None:
        with self.assertRaisesRegex(ValueError, "duplicate actor package source"):
            self.parse(
                """\
schema_version = 1
[[actors]]
path = "local_actor/bridge"
[[actors]]
path = "local_actor/bridge"
"""
            )

    def test_rejects_cmake_list_delimiters(self) -> None:
        with self.assertRaisesRegex(ValueError, "invalid delimiter"):
            self.parse(
                """\
schema_version = 1
[[actors]]
path = "local_actor/bridge;injected"
"""
            )


if __name__ == "__main__":
    unittest.main()
