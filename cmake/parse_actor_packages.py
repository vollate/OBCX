#!/usr/bin/env python3
"""Validate actors.toml and emit records for OBCXActorLoader.cmake."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys
import tomllib
from typing import Any
from urllib.parse import urlparse


def _safe_field(value: Any, field: str, index: int) -> str:
    if not isinstance(value, str) or not value.strip():
        raise ValueError(f"actors[{index}].{field} must be a non-empty string")
    value = value.strip()
    if ";" in value or "|" in value or "\n" in value or "\r" in value:
        raise ValueError(f"actors[{index}].{field} contains an invalid delimiter")
    return value


def parse_manifest(path: Path) -> list[tuple[str, str, str, str]]:
    try:
        with path.open("rb") as stream:
            document = tomllib.load(stream)
    except FileNotFoundError as error:
        raise ValueError(f"actors manifest does not exist: {path}") from error
    except tomllib.TOMLDecodeError as error:
        raise ValueError(f"invalid actors manifest TOML: {error}") from error

    unexpected = set(document) - {"schema_version", "actors"}
    if unexpected:
        raise ValueError(
            "unexpected actors manifest field(s): " + ", ".join(sorted(unexpected))
        )
    schema_version = document.get("schema_version")
    if (
        isinstance(schema_version, bool)
        or not isinstance(schema_version, int)
        or schema_version != 1
    ):
        raise ValueError("schema_version must equal 1")
    actors = document.get("actors")
    if not isinstance(actors, list):
        raise ValueError("actors must be an array of tables")

    records: list[tuple[str, str, str, str]] = []
    sources: set[str] = set()
    for index, actor in enumerate(actors):
        if not isinstance(actor, dict):
            raise ValueError(f"actors[{index}] must be a table")
        unexpected_fields = set(actor) - {
            "path",
            "repository",
            "revision",
            "enabled",
        }
        if unexpected_fields:
            raise ValueError(
                f"actors[{index}] has unsupported field(s): "
                + ", ".join(sorted(unexpected_fields))
            )
        enabled = actor.get("enabled", True)
        if not isinstance(enabled, bool):
            raise ValueError(f"actors[{index}].enabled must be a boolean")

        has_path = "path" in actor
        has_repository = "repository" in actor
        if has_path == has_repository:
            raise ValueError(
                f"actors[{index}] must declare exactly one of path or repository"
            )
        if has_path:
            if "revision" in actor:
                raise ValueError(
                    f"actors[{index}].revision is valid only for repositories"
                )
            source = _safe_field(actor["path"], "path", index)
            kind = "LOCAL"
            revision = ""
        else:
            source = _safe_field(actor["repository"], "repository", index)
            parsed = urlparse(source)
            if parsed.scheme != "https" or not parsed.netloc:
                raise ValueError(
                    f"actors[{index}].repository must be an https URL"
                )
            revision = _safe_field(actor.get("revision"), "revision", index)
            if revision == "HEAD":
                raise ValueError(
                    f"actors[{index}].revision must pin a tag, branch, or commit"
                )
            kind = "REMOTE"
        if source in sources:
            raise ValueError(f"duplicate actor package source: {source}")
        sources.add(source)
        records.append((kind, "true" if enabled else "false", source, revision))
    return records


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    args = parser.parse_args(argv)
    try:
        records = parse_manifest(args.manifest)
    except ValueError as error:
        print(f"{args.manifest}: {error}", file=sys.stderr)
        return 1
    for kind, enabled, source, revision in records:
        print("|".join((kind, enabled, source, revision)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
