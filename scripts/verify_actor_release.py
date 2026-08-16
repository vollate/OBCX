#!/usr/bin/env python3
"""Build and exercise an actor-only release from an empty work directory."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import time
from datetime import datetime, timezone
from typing import Any, Sequence


SOURCE_ROOT = Path(__file__).resolve().parents[1]
LOADER_ENVIRONMENT = (
    "LD_LIBRARY_PATH",
    "LD_PRELOAD",
    "DYLD_FALLBACK_LIBRARY_PATH",
    "DYLD_INSERT_LIBRARIES",
    "DYLD_LIBRARY_PATH",
)


class StepFailure(RuntimeError):
    """Raised when one release-verification command fails."""


def environment_prefix_to_cmake(value: str, separator: str = os.pathsep) -> str:
    """Translate the platform environment list into CMake cache-list syntax."""
    if not value:
        return ""
    return ";".join(entry for entry in value.split(separator) if entry)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def safe_clean_directory(work_dir: Path, source_dir: Path) -> None:
    work_dir = work_dir.resolve()
    source_dir = source_dir.resolve()
    if (
        work_dir == Path(work_dir.anchor)
        or work_dir == source_dir
        or work_dir in source_dir.parents
        or source_dir in work_dir.parents
    ):
        raise ValueError(
            "work directory must be an isolated directory outside the source tree"
        )
    if work_dir.exists():
        shutil.rmtree(work_dir)
    work_dir.mkdir(parents=True)


def one_artifact(directory: Path, stem: str) -> Path:
    matches = [path for path in directory.glob(f"{stem}.*") if path.is_file()]
    if len(matches) != 1:
        raise StepFailure(
            f"expected one installed {stem} actor in {directory}, found {matches}"
        )
    return matches[0]


def run_step(
    name: str,
    command: Sequence[str | Path],
    *,
    environment: dict[str, str],
    steps: list[dict[str, Any]],
    capture_output: bool = False,
) -> subprocess.CompletedProcess[str]:
    normalized = [str(part) for part in command]
    printable = normalized.copy()
    for index, part in enumerate(printable):
        if part.startswith("-DCMAKE_PREFIX_PATH="):
            printable[index] = "-DCMAKE_PREFIX_PATH=<dependency-prefix>"
    print(f"[{name}] {shlex.join(printable)}", flush=True)
    started = time.monotonic()
    result = subprocess.run(
        normalized,
        env=environment,
        check=False,
        text=True,
        capture_output=capture_output,
    )
    duration = time.monotonic() - started
    record: dict[str, Any] = {
        "name": name,
        "command": printable,
        "duration_seconds": round(duration, 3),
        "return_code": result.returncode,
    }
    if capture_output:
        record["stdout"] = result.stdout.strip()
        record["stderr"] = result.stderr.strip()
        if result.stdout:
            print(result.stdout, end="")
        if result.stderr:
            print(result.stderr, end="", file=sys.stderr)
    steps.append(record)
    if result.returncode != 0:
        raise StepFailure(f"{name} failed with exit code {result.returncode}")
    return result


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(
        description=(
            "Configure, build, install, start, load actors, process the "
            "representative pipeline, soak it, and shut down from a fresh tree"
        )
    )
    result.add_argument("--source", type=Path, default=SOURCE_ROOT)
    result.add_argument("--work-dir", type=Path, required=True)
    result.add_argument(
        "--report",
        type=Path,
        help="JSON result path (defaults to WORK_DIR/verification-report.json)",
    )
    result.add_argument("--generator", default="Ninja")
    result.add_argument("--jobs", type=int, default=2)
    result.add_argument(
        "--dependency-prefix",
        default=environment_prefix_to_cmake(
            os.environ.get("CMAKE_PREFIX_PATH", "")
        ),
        help=(
            "CMake semicolon-separated dependency prefix list; defaults to "
            "the platform-separated CMAKE_PREFIX_PATH environment value"
        ),
    )
    result.add_argument(
        "--soak-messages",
        type=int,
        default=1000,
        help="Messages processed by one continuously running installed runtime",
    )
    return result


def main() -> int:
    args = parser().parse_args()
    if args.jobs < 1 or args.soak_messages < 1:
        parser().error("--jobs and --soak-messages must be positive")

    source_dir = args.source.resolve()
    work_dir = args.work_dir.resolve()
    report_path = (
        args.report.resolve()
        if args.report is not None
        else work_dir / "verification-report.json"
    )
    safe_clean_directory(work_dir, source_dir)
    build_dir = work_dir / "build"
    deployment_dir = build_dir / "actor-package-conformance" / "sdk"

    environment = os.environ.copy()
    removed_loader_environment: dict[str, str] = {}
    for name in LOADER_ENVIRONMENT:
        if name in environment:
            removed_loader_environment[name] = environment.pop(name)
    environment.pop("CMAKE_PREFIX_PATH", None)
    environment["LC_ALL"] = "C"
    environment["TZ"] = "UTC"

    steps: list[dict[str, Any]] = []
    report: dict[str, Any] = {
        "schema_version": 1,
        "recorded_at": datetime.now(timezone.utc).isoformat(),
        "source": str(source_dir),
        "work_directory": str(work_dir),
        "generator": args.generator,
        "build_type": "Release",
        "jobs": args.jobs,
        "soak_messages": args.soak_messages,
        "loader_environment_removed": sorted(removed_loader_environment),
        "steps": steps,
        "status": "failed",
    }

    configure_command: list[str | Path] = [
        "cmake",
        "-S",
        source_dir,
        "-B",
        build_dir,
        "-G",
        args.generator,
        "-DCMAKE_BUILD_TYPE=Release",
        "-DOBCX_BUILD_TESTS=ON",
        "-DOBCX_BUILD_BENCHMARKS=OFF",
        "-DOBCX_ENABLE_CROSS_REPO_ACTOR_TESTS=ON",
    ]
    if args.dependency_prefix:
        configure_command.append(f"-DCMAKE_PREFIX_PATH={args.dependency_prefix}")

    try:
        run_step(
            "configure",
            configure_command,
            environment=environment,
            steps=steps,
        )
        run_step(
            "build",
            [
                "cmake",
                "--build",
                build_dir,
                "--parallel",
                str(args.jobs),
                "--target",
                "obcx",
                "message_store_actor",
                "bridge_actor",
            ],
            environment=environment,
            steps=steps,
        )
        run_step(
            "installed-cross-repository-pipeline",
            [
                "ctest",
                "--test-dir",
                build_dir,
                "--output-on-failure",
                "--no-tests=error",
                "-R",
                "^standalone_actor_v2_repositories$",
            ],
            environment=environment,
            steps=steps,
        )

        installed_obcx = deployment_dir / "bin" / "obcx"
        pipeline_smoke = (
            deployment_dir
            / "libexec"
            / "obcx"
            / "standalone_actor_pipeline_smoke"
        )
        actor_dir = deployment_dir / "lib" / "obcx" / "actors"
        message_store = one_artifact(actor_dir, "message_store")
        bridge = one_artifact(actor_dir, "bridge")
        runtime_libraries = sorted(deployment_dir.glob("lib*/libobcx_core.*"))
        if len(runtime_libraries) != 1 or not runtime_libraries[0].is_file():
            raise StepFailure(
                "expected one installed OBCX runtime library, found "
                f"{runtime_libraries}"
            )
        runtime_library = runtime_libraries[0]
        for required in (installed_obcx, pipeline_smoke):
            if not required.is_file():
                raise StepFailure(f"missing installed executable: {required}")

        version = run_step(
            "installed-application-start",
            [installed_obcx, "--version"],
            environment=environment,
            steps=steps,
            capture_output=True,
        )
        if "OBCX Robot Framework v" not in version.stdout:
            raise StepFailure("installed OBCX returned an unexpected version banner")

        soak = run_step(
            "installed-runtime-soak",
            [pipeline_smoke, message_store, bridge, str(args.soak_messages)],
            environment=environment,
            steps=steps,
            capture_output=True,
        )
        expected_soak = (
            f"messages={args.soak_messages} persisted={args.soak_messages} "
            f"forwarded={args.soak_messages} failures=0"
        )
        if soak.stdout.strip() != expected_soak:
            raise StepFailure(
                "installed actor pipeline returned unexpected soak metrics"
            )
        report["deployment"] = {
            "root": str(deployment_dir),
            "artifacts": {
                name: {
                    "path": str(path.relative_to(deployment_dir)),
                    "sha256": sha256(path),
                }
                for name, path in (
                    ("obcx", installed_obcx),
                    ("runtime_library", runtime_library),
                    ("pipeline_smoke", pipeline_smoke),
                    ("message_store", message_store),
                    ("bridge", bridge),
                )
            },
        }
        report["soak"] = {
            "messages": args.soak_messages,
            "persisted": args.soak_messages,
            "forwarded": args.soak_messages,
            "failures": 0,
        }
        report["status"] = "passed"
    except (OSError, StepFailure, ValueError) as error:
        report["failure"] = str(error)
        print(f"release verification failed: {error}", file=sys.stderr)
    finally:
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        print(f"verification report: {report_path}")

    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
