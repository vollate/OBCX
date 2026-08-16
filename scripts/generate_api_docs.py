#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path
import re
import shutil
import subprocess


ROOT = Path(__file__).resolve().parents[1]


def joined(*parts: str) -> str:
    return "".join(parts)


RETIRED_PATTERNS = (
    re.compile(re.escape(joined("I", "Plugin"))),
    re.compile(re.escape(joined("Plugin", "Manager"))),
    re.compile(re.escape(joined("OBCX_", "PLUGIN", "_EXPORT"))),
    re.compile(re.escape(joined("OBCX", "Plugin"))),
    re.compile(r"\bIActor\b"),
    re.compile(re.escape(joined("AsioActor", "V1Adapter"))),
    re.compile(re.escape(joined("allow_", "v1_actors"))),
    re.compile(re.escape(joined("ActorScheduler", "Engine"))),
    re.compile(r"\bActorScheduler\b"),
    re.compile(re.escape(joined("core/actor_", "scheduler.hpp"))),
    re.compile(re.escape(joined("asio", "-v1"))),
)


def generate(doxygen: str) -> None:
    for language in ("en", "zh"):
        shutil.rmtree(ROOT / "docs" / "api" / language, ignore_errors=True)
    subprocess.run([doxygen, "Doxyfile"], cwd=ROOT, check=True)
    subprocess.run([doxygen, "Doxyfile.zh"], cwd=ROOT, check=True)


def audit() -> None:
    findings: list[str] = []
    for language in ("en", "zh"):
        html_root = ROOT / "docs" / "api" / language / "html"
        if not (html_root / "index.html").is_file():
            findings.append(f"missing generated {language} API index")
            continue
        for path in html_root.rglob("*"):
            if not path.is_file():
                continue
            lowered = path.name.lower()
            if joined("plugin") in lowered or joined("asio_actor_", "v1") in lowered:
                findings.append(str(path.relative_to(ROOT)))
                continue
            if path.suffix not in {".html", ".js", ".xml"}:
                continue
            content = path.read_text(encoding="utf-8", errors="ignore")
            for pattern in RETIRED_PATTERNS:
                if pattern.search(content):
                    findings.append(
                        f"{path.relative_to(ROOT)}: {pattern.pattern}"
                    )
    if findings:
        raise RuntimeError(
            "generated API documentation exposes retired surfaces:\n"
            + "\n".join(findings)
        )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate and audit English/Chinese actor-only API docs"
    )
    parser.add_argument("--doxygen", default="doxygen")
    parser.add_argument("--audit-only", action="store_true")
    args = parser.parse_args()

    if not args.audit_only:
        generate(args.doxygen)
    audit()
    print("valid actor-only API documentation: English, Chinese")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
