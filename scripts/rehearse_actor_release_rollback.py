#!/usr/bin/env python3
"""Rehearse an atomic deployment rollback between two installed OBCX trees."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
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


class RehearsalFailure(RuntimeError):
    """Raised when a deployment or health-check operation fails."""


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def reset_work_directory(path: Path, protected: Sequence[Path]) -> None:
    resolved = path.resolve()
    if resolved == Path(resolved.anchor):
        raise ValueError("deployment work directory cannot be a filesystem root")
    for item in protected:
        protected_path = item.resolve()
        if (
            resolved == protected_path
            or resolved in protected_path.parents
            or protected_path in resolved.parents
        ):
            raise ValueError(
                "deployment work directory must be separate from source and releases"
            )
    if resolved.exists():
        shutil.rmtree(resolved)
    resolved.mkdir(parents=True)


def run_health_check(
    name: str,
    command: Sequence[str | Path],
    *,
    environment: dict[str, str],
    steps: list[dict[str, Any]],
) -> str:
    normalized = [str(part) for part in command]
    print(f"[{name}] {shlex.join(normalized)}", flush=True)
    started = time.monotonic()
    result = subprocess.run(
        normalized,
        env=environment,
        check=False,
        text=True,
        capture_output=True,
    )
    duration = time.monotonic() - started
    steps.append(
        {
            "name": name,
            "command": normalized,
            "duration_seconds": round(duration, 3),
            "return_code": result.returncode,
            "stdout": result.stdout.strip(),
            "stderr": result.stderr.strip(),
        }
    )
    if result.stdout:
        print(result.stdout, end="")
    if result.stderr:
        print(result.stderr, end="", file=sys.stderr)
    if result.returncode != 0:
        raise RehearsalFailure(
            f"{name} failed with exit code {result.returncode}"
        )
    return result.stdout


def atomic_switch(
    deployment_root: Path,
    release_name: str,
    steps: list[dict[str, Any]],
) -> Path:
    current = deployment_root / "current"
    temporary = deployment_root / ".current.next"
    temporary.unlink(missing_ok=True)
    target = Path("releases") / release_name
    temporary.symlink_to(target, target_is_directory=True)
    os.replace(temporary, current)
    resolved = current.resolve(strict=True)
    expected = (deployment_root / target).resolve(strict=True)
    if resolved != expected:
        raise RehearsalFailure(
            f"atomic switch resolved to {resolved}, expected {expected}"
        )
    steps.append(
        {
            "name": f"switch-to-{release_name}",
            "target": str(target),
            "resolved_target": str(resolved),
            "return_code": 0,
        }
    )
    print(f"[switch-to-{release_name}] current -> {target}", flush=True)
    return current


def find_library_directory(release_root: Path) -> Path:
    candidates = [release_root / "lib64", release_root / "lib"]
    for candidate in candidates:
        if any(candidate.glob("libobcx_core.*")):
            return candidate
    raise RehearsalFailure(f"no installed OBCX core library in {release_root}")


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--candidate", type=Path, required=True)
    result.add_argument("--previous", type=Path, required=True)
    result.add_argument("--work-dir", type=Path, required=True)
    result.add_argument("--previous-revision", required=True)
    result.add_argument("--report", type=Path)
    result.add_argument("--candidate-health-messages", type=int, default=100)
    return result


def main() -> int:
    args = parser().parse_args()
    if args.candidate_health_messages < 1:
        parser().error("--candidate-health-messages must be positive")
    if re.fullmatch(r"[0-9a-f]{40}", args.previous_revision) is None:
        parser().error("--previous-revision must be a full Git SHA")
    candidate = args.candidate.resolve()
    previous = args.previous.resolve()
    work_dir = args.work_dir.resolve()
    report_path = (
        args.report.resolve()
        if args.report is not None
        else work_dir / "rollback-report.json"
    )
    reset_work_directory(work_dir, (SOURCE_ROOT, candidate, previous))

    releases = work_dir / "releases"
    releases.mkdir()
    (releases / "candidate").symlink_to(candidate, target_is_directory=True)
    (releases / "previous").symlink_to(previous, target_is_directory=True)

    clean_environment = os.environ.copy()
    for name in LOADER_ENVIRONMENT:
        clean_environment.pop(name, None)
    clean_environment["LC_ALL"] = "C"
    clean_environment["TZ"] = "UTC"

    previous_environment = clean_environment.copy()
    previous_library = find_library_directory(previous)
    if platform.system() == "Darwin":
        previous_environment["DYLD_FALLBACK_LIBRARY_PATH"] = str(
            previous_library
        )
    else:
        previous_environment["LD_LIBRARY_PATH"] = str(previous_library)

    steps: list[dict[str, Any]] = []
    report: dict[str, Any] = {
        "schema_version": 1,
        "recorded_at": datetime.now(timezone.utc).isoformat(),
        "candidate": {
            "root": str(candidate),
            "binary_sha256": sha256(candidate / "bin" / "obcx"),
            "artifacts": {
                name: sha256(path)
                for name, path in (
                    ("pipeline_smoke", candidate / "libexec" / "obcx" / "standalone_actor_pipeline_smoke"),
                    ("message_store", candidate / "lib" / "obcx" / "actors" / "message_store.so"),
                    ("bridge", candidate / "lib" / "obcx" / "actors" / "bridge.so"),
                )
            },
        },
        "previous": {
            "revision": args.previous_revision,
            "root": str(previous),
            "binary_sha256": sha256(previous / "bin" / "obcx"),
        },
        "candidate_health_messages": args.candidate_health_messages,
        "steps": steps,
        "status": "failed",
    }

    try:
        current = atomic_switch(work_dir, "candidate", steps)
        candidate_version = run_health_check(
            "candidate-version-health",
            [current / "bin" / "obcx", "--version"],
            environment=clean_environment,
            steps=steps,
        )
        if "actor-based bot framework" not in candidate_version:
            raise RehearsalFailure("candidate version banner is not actor-only")

        actor_dir = current / "lib" / "obcx" / "actors"
        pipeline_health = run_health_check(
            "candidate-pipeline-health",
            [
                current
                / "libexec"
                / "obcx"
                / "standalone_actor_pipeline_smoke",
                actor_dir / "message_store.so",
                actor_dir / "bridge.so",
                str(args.candidate_health_messages),
            ],
            environment=clean_environment,
            steps=steps,
        )
        expected_pipeline_health = (
            f"messages={args.candidate_health_messages} "
            f"persisted={args.candidate_health_messages} "
            f"forwarded={args.candidate_health_messages} failures=0"
        )
        if pipeline_health.strip() != expected_pipeline_health:
            raise RehearsalFailure(
                "candidate pipeline returned unexpected health metrics"
            )

        current = atomic_switch(work_dir, "previous", steps)
        previous_version = run_health_check(
            "previous-version-health",
            [current / "bin" / "obcx", "--version"],
            environment=previous_environment,
            steps=steps,
        )
        if "modular bot framework" not in previous_version:
            raise RehearsalFailure("previous version banner is unexpected")
        run_health_check(
            "previous-cli-health",
            [current / "bin" / "obcx", "--help"],
            environment=previous_environment,
            steps=steps,
        )
        report["final_deployment_target"] = str(current.resolve(strict=True))
        report["status"] = "passed"
    except (OSError, RehearsalFailure, ValueError) as error:
        report["failure"] = str(error)
        print(f"rollback rehearsal failed: {error}", file=sys.stderr)
    finally:
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        print(f"rollback report: {report_path}")

    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
