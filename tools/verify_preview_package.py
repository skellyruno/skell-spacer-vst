#!/usr/bin/env python3
"""Strict preview bundle verifier for the staging directory and downloaded ZIP."""
from __future__ import annotations
import argparse
import hashlib
from pathlib import Path
import re
import zipfile

BINARY = "Skell Spacer.vst3/Contents/x86_64-win/Skell Spacer.vst3"
REQUIRED = {BINARY, "Skell Spacer.vst3/Contents/Resources/moduleinfo.json",
            "build-info.json", "SHA256SUMS.txt", "Install-SkellSpacerPreview.ps1",
            "THIRD_PARTY_NOTICES.md", "WINDOWS-VST3-INSTALL.md"}


def check_names(names: list[str]) -> None:
    if any(not n or "\\" in n or ":" in n or n.startswith("/") or
           any(part in {"", ".", ".."} for part in n.split("/")) for n in names):
        raise ValueError("unsafe package path")
    if len(set(n.casefold() for n in names)) != len(names):
        raise ValueError("duplicate package path (Windows case-insensitive)")
    if set(names) != REQUIRED:
        raise ValueError(f"unexpected package files: missing={REQUIRED-set(names)}, extra={set(names)-REQUIRED}")


def check_files(names, read) -> str:
    check_names(names)
    checks = {}
    for line in read("SHA256SUMS.txt").decode("utf-8-sig").splitlines():
        m = re.fullmatch(r"([0-9A-Fa-f]{64})  (.+)", line)
        if not m or m[2] in checks:
            raise ValueError("malformed or duplicate checksum entry")
        checks[m[2]] = m[1].lower()
    if set(checks) != REQUIRED - {"SHA256SUMS.txt"}:
        raise ValueError("checksum inventory does not exactly cover package")
    for name, expected in checks.items():
        if hashlib.sha256(read(name)).hexdigest() != expected:
            raise ValueError(f"checksum mismatch: {name}")
    return checks[BINARY]


def verify(path: Path) -> str:
    if path.is_dir():
        entries = list(path.rglob("*"))
        if any(p.is_symlink() for p in entries):
            raise ValueError("symlink in staging directory")
        files = [p.relative_to(path).as_posix() for p in entries if p.is_file()]
        return check_files(files, lambda name: (path/name).read_bytes())
    with zipfile.ZipFile(path) as z:
        # Reject unsafe paths and duplicate entries including directories before reading.
        all_names = [entry.filename.rstrip("/") for entry in z.infolist()]
        if len(set(n.casefold() for n in all_names)) != len(all_names):
            raise ValueError("duplicate ZIP entries")
        if any(not n or "\\" in n or ":" in n or n.startswith("/") or
               any(part in {"", ".", ".."} for part in n.split("/")) for n in all_names):
            raise ValueError("unsafe ZIP path")
        files = [entry.filename for entry in z.infolist() if not entry.is_dir()]
        check_names(files)
        if z.testzip() is not None:
            raise ValueError("ZIP CRC failure")
        return check_files(files, z.read)


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("package", type=Path)
    args = p.parse_args()
    print(f"PASS: exact package inventory and checksums; VST3 SHA256 {verify(args.package)}")
