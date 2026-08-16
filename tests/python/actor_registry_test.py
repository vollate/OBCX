from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
REGISTRY_ROOT = ROOT / "actor-registry"
GENERATOR = REGISTRY_ROOT / "generate_actor_index.py"
SPEC = importlib.util.spec_from_file_location("actor_registry", GENERATOR)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot import {GENERATOR}")
registry = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(registry)


class ActorRegistryTest(unittest.TestCase):
    def test_publishes_bridge_and_message_store_entries(self) -> None:
        index = registry.build_index(REGISTRY_ROOT / "entries")
        self.assertEqual(index["schema_version"], 1)
        self.assertEqual(
            [actor["id"] for actor in index["actors"]],
            ["onebot-cxx.message-store", "vollate.bridge"],
        )
        for actor in index["actors"]:
            self.assertEqual(actor["abi"], 2)
            self.assertEqual(
                actor["artifact"]["platforms"],
                ["linux-x86_64", "linux-arm64"],
            )
            self.assertEqual(
                list(actor["artifact"]["files"]),
                ["linux-x86_64", "linux-arm64"],
            )
            self.assertEqual(
                actor["artifact"]["entrypoint"], "obcx_create_actor_v2"
            )

    def test_invalid_actor_metadata_is_rejected_by_field(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            entries = Path(temporary) / "entries"
            actor_dir = entries / "vollate.bridge"
            actor_dir.mkdir(parents=True)
            source = REGISTRY_ROOT / "entries/vollate.bridge/actor.toml"
            content = source.read_text(encoding="utf-8").replace(
                'description = "QQ and Telegram bidirectional bridge actor for OBCX"\n',
                "",
            )
            (actor_dir / "actor.toml").write_text(content, encoding="utf-8")
            with self.assertRaises(registry.metadata.ActorMetadataError) as error:
                registry.build_index(entries)
        self.assertIn(
            "[publication].description must be a non-empty string",
            str(error.exception),
        )

    def test_index_generation_is_byte_deterministic(self) -> None:
        entries = REGISTRY_ROOT / "entries"
        first = registry.encoded_index(registry.build_index(entries))
        second = registry.encoded_index(registry.build_index(entries))
        self.assertEqual(first, second)
        committed = (REGISTRY_ROOT / "index/actors.json").read_text(
            encoding="utf-8"
        )
        self.assertEqual(first, committed)

    def test_resolves_platform_artifact(self) -> None:
        index = registry.build_index(REGISTRY_ROOT / "entries")
        result = registry.resolve_artifact(
            index, "vollate.bridge", "0.1.0", "linux-x86_64"
        )
        self.assertEqual(result["filename"], "bridge-linux-x86_64.so")
        self.assertEqual(result["abi"], 2)
        self.assertEqual(result["entrypoint"], "obcx_create_actor_v2")
        self.assertEqual(result["install_directory"], "lib/obcx/actors")
        self.assertEqual(
            result["url"],
            "https://github.com/vollate/obcx-actor-bridge/"
            "releases/download/v0.1.0/bridge-linux-x86_64.so",
        )

    def test_does_not_invent_an_unpublished_platform(self) -> None:
        index = registry.build_index(REGISTRY_ROOT / "entries")
        with self.assertRaisesRegex(
            registry.RegistryError, "unsupported artifact platform"
        ):
            registry.resolve_artifact(
                index, "vollate.bridge", "0.1.0", "macos-arm64"
            )

    def test_resolves_supported_arm64_artifact(self) -> None:
        index = registry.build_index(REGISTRY_ROOT / "entries")
        result = registry.resolve_artifact(
            index, "vollate.bridge", "0.1.0", "linux-arm64"
        )
        self.assertEqual(result["filename"], "bridge-linux-arm64.so")

    def test_registry_entries_match_standalone_packages(self) -> None:
        pairs = {
            "onebot-cxx.message-store": ROOT
            / "local_actor/obcx-actor-message-store/actor.toml",
            "vollate.bridge": ROOT / "local_actor/obcx-actor-bridge/actor.toml",
        }
        for actor_id, standalone in pairs.items():
            registry_entry = REGISTRY_ROOT / "entries" / actor_id / "actor.toml"
            self.assertEqual(
                registry.metadata.require_metadata(registry_entry),
                registry.metadata.require_metadata(standalone),
            )

    def test_independent_registry_matches_canonical_contract(self) -> None:
        independent = ROOT / "local_actor/obcx-actor-registry"
        self.assertEqual(
            (ROOT / "cmake/actor_metadata.py").read_bytes(),
            (independent / "scripts/actor_metadata.py").read_bytes(),
        )
        self.assertEqual(
            (REGISTRY_ROOT / "index/actors.json").read_bytes(),
            (independent / "index/actors.json").read_bytes(),
        )
        for relative in (
            "schemas/actor-index.schema.json",
            "schemas/actor-registry-entry.schema.json",
        ):
            self.assertEqual(
                (REGISTRY_ROOT / relative).read_bytes(),
                (independent / relative).read_bytes(),
            )
        for actor_id in ("onebot-cxx.message-store", "vollate.bridge"):
            relative = Path("entries") / actor_id / "actor.toml"
            self.assertEqual(
                (REGISTRY_ROOT / relative).read_bytes(),
                (independent / relative).read_bytes(),
            )

    def test_registry_schemas_are_actor_named_and_closed(self) -> None:
        entry_schema = json.loads(
            (REGISTRY_ROOT / "schemas/actor-registry-entry.schema.json").read_text(
                encoding="utf-8"
            )
        )
        index_schema = json.loads(
            (REGISTRY_ROOT / "schemas/actor-index.schema.json").read_text(
                encoding="utf-8"
            )
        )
        self.assertFalse(entry_schema["additionalProperties"])
        self.assertEqual(entry_schema["properties"]["abi"]["const"], 2)
        self.assertEqual(index_schema["properties"]["schema_version"]["const"], 1)


if __name__ == "__main__":
    unittest.main()
