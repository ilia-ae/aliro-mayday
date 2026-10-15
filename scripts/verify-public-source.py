#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Verify the public file manifest, reviewed fixture hashes and current doc links.

This checks integrity and coverage, not the absence of secrets. A separate
secret scan and review are required when publishing new source content.
"""
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parents[1]
CURRENT_DOCS = (
    "README.md", "README.ru.md", "THIRD_PARTY_NOTICES.md", "docs/README.md",
    "docs/repository-status-2026-10-14.md", "docs/repository-status-2026-10-14.ru.md",
    "docs/README.ru.md", "aliro-site/README.md", "aliro-site/README.ru.md",
    "docs/licensing.md", "docs/licensing.ru.md",
    "CONTRIBUTING.md", "CONTRIBUTING.ru.md", ".github/PULL_REQUEST_TEMPLATE.md",
    "AUTHORS.md", "AUTHORS.ru.md",
    "docs/provenance/2026-10-15/README.md", "docs/provenance/2026-10-15/README.ru.md",
    "docs/wiring.ru.md", "docs/wiring.md",
    "docs/samsung-bench-preparation-2026-10-14.ru.md",
    "docs/samsung-bench-preparation-2026-10-14.md",
    "docs/aliro-access-policy-2026-10-14.ru.md",
    "docs/aliro-access-policy-2026-10-14.md",
    "docs/aliro-competitors-2026-10-13.ru.md",
    "docs/aliro-competitors-2026-10-13.md",
    "aliro-esp32/README.md", "aliro-esp32/README.ru.md",
    "aliro-esp32/tests/README.md", "aliro-esp32/tests/README.ru.md",
)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify() -> list[str]:
    errors = []
    manifest = json.loads((ROOT / "SOURCE_MANIFEST.json").read_text())
    if manifest.get("format") != 1 or manifest.get("history_included") is not False:
        errors.append("Unexpected manifest format or history flag")
    names = [item["path"] for item in manifest["files"]]
    if len(set(names)) != len(names):
        errors.append("Duplicate manifest paths")
    tracked = set(subprocess.check_output(
        ["git", "-C", str(ROOT), "ls-files", "-z"], text=True).split("\0")) - {""}
    expected = tracked - {"SOURCE_MANIFEST.json"}
    for name in sorted(expected - set(names)):
        errors.append(f"Tracked file missing from manifest: {name}")
    for name in sorted(set(names) - expected):
        errors.append(f"Manifest file not tracked: {name}")
    for item in manifest["files"]:
        name = item["path"]
        pure = PurePosixPath(name)
        if pure.is_absolute() or ".." in pure.parts:
            errors.append(f"Unsafe manifest path: {name}")
            continue
        path = ROOT / name
        if path.is_symlink() or not path.is_file():
            errors.append(f"Missing or non-regular source file: {name}")
        elif sha256(path) != item["sha256"]:
            errors.append(f"Manifest hash mismatch: {name}")
    prohibited_parts = {".git", "build", "dist", "managed_components",
                        "node_modules", ".venv", "__pycache__"}
    prohibited_names = {"sdkconfig", "sdkconfig.old", "dependencies.lock",
                        "allowlist.json", "events.jsonl", "state.json", "control.json"}
    for name in sorted(tracked):
        path = PurePosixPath(name)
        if (prohibited_parts.intersection(path.parts)
                or path.name in prohibited_names or path.name.startswith(".env")
                or path.suffix in {".pdf", ".log", ".trace", ".pyc"}):
            errors.append(f"Excluded local/runtime file is tracked: {name}")
    review = json.loads((ROOT / "docs/public-source-review-2026-10-13.json").read_text())
    for finding in review["findings"]:
        path = ROOT / finding["file"]
        if not path.is_file() or sha256(path) != finding["sha256"]:
            errors.append(f"Reviewed fixture changed: {finding['file']}")
    for name in CURRENT_DOCS:
        doc = ROOT / name
        for target in re.findall(r"!?\[[^\]]*\]\(([^\s)]+)\)", doc.read_text()):
            if target.startswith(("https://", "http://", "mailto:", "#")):
                continue
            relative = unquote(target.split("#", 1)[0].split("?", 1)[0]).strip("<>")
            resolved = (doc.parent / relative).resolve()
            if not resolved.is_relative_to(ROOT) or not resolved.exists():
                errors.append(f"Broken local documentation link in {name}: {target}")
    return errors


def main() -> int:
    try:
        errors = verify()
    except (KeyError, ValueError, OSError, subprocess.CalledProcessError) as exc:
        print(f"Source verification failed: {exc}", file=sys.stderr)
        return 1
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print("Public source manifest, reviewed fixture hashes and current doc links verified")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
