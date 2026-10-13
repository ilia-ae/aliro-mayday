"""Minimal sanity tests — no PM3 required."""

from __future__ import annotations

from pathlib import Path

from lock_mvp import Allowlist, LockState, normalize_uid


def test_normalize_uid_strips_spaces_uppercases() -> None:
    assert normalize_uid("04 53 FE b2 87 10 90") == "0453FEB2871090"


def test_allowlist_roundtrip(tmp_path: Path) -> None:
    p = tmp_path / "al.json"
    al = Allowlist(path=p)
    al.load()
    assert al.entries == {}
    al.add("04abcd", "test", "HF14A")
    assert al.contains("04ABCD")
    assert al.describe("04abcd") == "test"

    al2 = Allowlist(path=p)
    al2.load()
    assert al2.entries == al.entries
    assert al2.describe("04ABCD") == "test"


def test_lockstate_toggle() -> None:
    s = LockState()
    assert s.state == "CLOSED"
    s.toggle("04abcd")
    assert s.state == "OPEN"
    assert s.last_uid == "04abcd"
    s.toggle("04abcd")
    assert s.state == "CLOSED"


def test_allowlist_missing_describe() -> None:
    al = Allowlist(path=Path("/nonexistent/do-not-create.json"))
    al.entries = {}
    assert al.describe("aabbcc") == "<unknown>"
    assert not al.contains("aabbcc")
