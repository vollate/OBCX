#!/usr/bin/env python3
"""Merge OBCX core dependencies with canonical actor.toml dependencies.

Usage:
    python3 cmake/gen_vcpkg_manifest.py [actors.toml] --binary-dir build/actor-dev
    python3 cmake/gen_vcpkg_manifest.py [actors.toml] --binary-dir build/actor-dev --list
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys
from typing import Any


SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from actor_metadata import require_metadata  # noqa: E402
from parse_actor_packages import parse_manifest  # noqa: E402


def _remote_source(binary_dir: Path, repository: str, revision: str) -> Path:
    key = hashlib.md5(
        f"{repository}@{revision}".encode("utf-8"), usedforsecurity=False
    ).hexdigest()
    candidate = binary_dir / "_actors" / key
    if candidate.is_dir():
        return candidate
    raise ValueError(
        f"remote actor {repository}@{revision} has not been fetched; "
        f"configure the actors.toml build in {binary_dir} before generating "
        "vcpkg.json"
    )


def collect_actor_dependencies(
    actors_manifest: Path, project_root: Path, binary_dir: Path | None = None
) -> tuple[list[str], dict[str, list[str]]]:
    if binary_dir is None:
        binary_dir = project_root / "build"
    dependencies: list[str] = []
    per_actor: dict[str, list[str]] = {}
    for kind, enabled, source, revision in parse_manifest(actors_manifest):
        if enabled != "true":
            continue
        if kind == "LOCAL":
            actor_dir = Path(source)
            if not actor_dir.is_absolute():
                actor_dir = project_root / actor_dir
        else:
            actor_dir = _remote_source(binary_dir, source, revision)
        document = require_metadata(actor_dir / "actor.toml")
        actor_id = document["actor"]["id"]
        packages = list(document["dependencies"]["packages"])
        if actor_id in per_actor:
            raise ValueError(f"duplicate actor id in package set: {actor_id}")
        per_actor[actor_id] = packages
        dependencies.extend(packages)
    return dependencies, per_actor


def _dependency_key(dependency: Any) -> str:
    return json.dumps(dependency, sort_keys=True, separators=(",", ":"))


def merge_dependencies(
    base_dependencies: list[Any], actor_dependencies: list[str]
) -> list[Any]:
    merged = {_dependency_key(item): item for item in base_dependencies}
    for package in actor_dependencies:
        merged.setdefault(_dependency_key(package), package)
    return [merged[key] for key in sorted(merged)]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", nargs="?", type=Path)
    parser.add_argument("--list", action="store_true", dest="list_only")
    parser.add_argument("--output", type=Path)
    parser.add_argument(
        "--binary-dir",
        type=Path,
        default=Path("build"),
        help="CMake binary directory containing the _actors fetch tree",
    )
    args = parser.parse_args(argv)

    project_root = SCRIPT_DIR.parent
    manifest = args.manifest or project_root / "actors.toml"
    if not manifest.is_absolute():
        manifest = project_root / manifest
    binary_dir = args.binary_dir
    if not binary_dir.is_absolute():
        binary_dir = project_root / binary_dir
    base_path = project_root / "vcpkg-base.json"
    output_path = args.output or project_root / "vcpkg.json"

    try:
        base = json.loads(base_path.read_text(encoding="utf-8"))
        actor_dependencies, per_actor = collect_actor_dependencies(
            manifest, project_root, binary_dir
        )
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    base_dependencies = list(base.get("dependencies", []))
    merged = merge_dependencies(base_dependencies, actor_dependencies)
    if args.list_only:
        print("=== OBCX core dependencies ===")
        for dependency in base_dependencies:
            print(f"  {_dependency_key(dependency)}")
        if per_actor:
            print("\n=== Actor package dependencies ===")
            for actor_id, packages in sorted(per_actor.items()):
                print(f"  [{actor_id}]")
                for package in packages:
                    print(f"    {package}")
        print(f"\n=== All required packages ({len(merged)}) ===")
        for dependency in merged:
            print(f"  {_dependency_key(dependency)}")
        return 0

    base["dependencies"] = merged
    output_path.write_text(json.dumps(base, indent=2) + "\n", encoding="utf-8")
    base_package_names = {
        dependency
        for dependency in base_dependencies
        if isinstance(dependency, str)
    }
    added = sorted(set(actor_dependencies) - base_package_names)
    print(f"Generated {output_path}")
    print(
        f"  Base deps: {len(base_dependencies)}, "
        f"actor deps: {len(added)}, total: {len(merged)}"
    )
    if added:
        print(f"  Added by actors: {', '.join(added)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
