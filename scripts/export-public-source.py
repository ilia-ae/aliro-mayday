#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Create a reviewable source archive without Git history or local runtime data."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
EXCLUDED_NAMES = {".git", ".env", ".env.local", "sdkconfig", "sdkconfig.old",
                  "dependencies.lock", "allowlist.json", "events.jsonl", "state.json", "control.json"}
EXCLUDED_PARTS = {"build", "dist", "managed_components", "node_modules", ".venv", "__pycache__"}
EXCLUDED_PATHS = {"littlefs.bin", "docs/aliro-audit-2026-10-13.md"}


def git(*args: str) -> bytes:
    return subprocess.check_output(["git", "-C", str(ROOT), *args])


def excluded(name: str) -> bool:
    path = PurePosixPath(name)
    return (name in EXCLUDED_PATHS or any(part in EXCLUDED_PARTS for part in path.parts)
            or path.name in EXCLUDED_NAMES or path.name.startswith(".env")
            or path.suffix in {".pdf", ".log", ".trace", ".pyc"})


def known_local_secrets() -> list[bytes]:
    values = []
    # Check the old literals without ever printing them or copying history.
    try:
        old = git("show", "master:matter-controller/main/app_main.cpp").decode()
        values.extend(value.encode() for value in re.findall(
            r'kWifi(?:Ssid|Pass)\s*=\s*"([^"\n]+)"', old))
    except subprocess.CalledProcessError:
        pass
    env = ROOT / ".env"
    if env.exists():
        for line in env.read_text().splitlines():
            if "=" in line and not line.lstrip().startswith("#"):
                name, value = line.split("=", 1)
                if name.strip().startswith("TELEGRAM_"):
                    value = value.strip().strip('"\'')
                    if len(value) >= 8:
                        values.append(value.encode())
    return values


def sanitize_documentation(name: str, contents: bytes) -> bytes:
    # Retain third-party notices and source verbatim; replace personal network
    # examples only in project-authored documentation.
    if name == "justfile":
        return re.sub(rb"\b192\.168\.\d{1,3}\.\d{1,3}\b", b"192.0.2.10", contents)
    if name == "aliro-esp32/sdkconfig.defaults":
        return contents.replace(b"D83BDAA2EBA4", b"AABBCCDDEEFF")  # illustrative comment
    if name == "aliro-site/frontend/src/app/pages/landing/landing.ts":
        return re.sub(rb"/Users/[^/]+/(?:CODE/)?", b"/path/to/", contents)
    if not name.endswith(".md") or "/components/" in name or name.startswith("homekey-esp32/"):
        return contents
    text = contents.decode("utf-8")
    text = re.sub(r"\b192\.168\.\d{1,3}\.\d{1,3}\b", "192.0.2.10", text)
    text = re.sub(r"/Users/[^/]+/(?:CODE/)?", "/path/to/", text)
    return text.encode()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "dist/public-source-2026-10-13")
    args = parser.parse_args()
    destination = args.output.resolve()
    if destination.exists():
        raise SystemExit("Output already exists; choose a new directory.")
    if destination == ROOT or ROOT in destination.parents and destination.parts[len(ROOT.parts)] != "dist":
        raise SystemExit("Use dist/ or a directory outside the checkout.")
    names = sorted({name.decode() for name in
                    git("ls-files", "-z", "--cached", "--others", "--exclude-standard").split(b"\0") if name})
    secrets = known_local_secrets()
    prepared = []
    manifest = []
    omitted = []
    for name in names:
        path = ROOT / name
        if excluded(name):
            omitted.append(name)
            continue
        if not path.exists():  # files deleted by the dependency update
            continue
        if path.is_symlink() or not path.is_file() or ROOT not in path.resolve().parents:
            raise SystemExit(f"Unsupported source entry: {name}")
        contents = sanitize_documentation(name, path.read_bytes())
        if any(secret and secret in contents for secret in secrets):
            raise SystemExit(f"Known local credential found in {name}; export stopped.")
        prepared.append((name, contents, path.stat().st_mode & 0o777))
        manifest.append({"path": name, "sha256": hashlib.sha256(contents).hexdigest()})
    destination.mkdir(parents=True)
    for name, contents, mode in prepared:
        path = destination / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(contents)
        path.chmod(mode)
    (destination / "SOURCE_MANIFEST.json").write_text(json.dumps({
        "format": 1, "history_included": False, "files": manifest, "omitted": omitted,
    }, indent=2) + "\n")
    archive = destination.with_suffix(".zip")
    if archive.exists():
        shutil.rmtree(destination)
        raise SystemExit("Archive already exists; choose a new output path.")
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as output:
        for path in sorted(destination.rglob("*")):
            if path.is_file():
                output.write(path, arcname=f"{destination.name}/{path.relative_to(destination)}")
    print(json.dumps({"directory": str(destination), "archive": str(archive),
                      "files": len(manifest), "known_local_credentials_found": 0,
                      "archive_sha256": hashlib.sha256(archive.read_bytes()).hexdigest()}))


if __name__ == "__main__":
    main()
