#!/usr/bin/env python3
"""Normalize JUCE-generated VST3 moduleinfo.json and validate strict JSON."""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import Any

_VERSION_RE = re.compile(r"^\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?$")
_CID_RE = re.compile(r"^[0-9A-Fa-f]{32}$")
_EXPECTED_CLASS_COUNT = 2
_REQUIRED_CATEGORIES = {"Audio Module Class", "Component Controller Class"}


def strip_trailing_commas(source: str) -> str:
    """Remove commas immediately before } or ] without touching quoted text."""
    output: list[str] = []
    in_string = False
    escaped = False
    index = 0

    while index < len(source):
        char = source[index]

        if in_string:
            output.append(char)
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                in_string = False
            index += 1
            continue

        if char == '"':
            in_string = True
            output.append(char)
            index += 1
            continue

        if char == ",":
            lookahead = index + 1
            while lookahead < len(source) and source[lookahead] in " \t\r\n":
                lookahead += 1
            if lookahead < len(source) and source[lookahead] in "}]":
                # Preserve whitespace while dropping only the structural comma.
                output.append(source[index + 1 : lookahead])
                index = lookahead
                continue

        output.append(char)
        index += 1

    return "".join(output)


def validate_moduleinfo(
    document: Any,
    expected_name: str | None = None,
    expected_vendor: str | None = None,
) -> dict[str, Any]:
    """Validate the VST3 identity and processor/controller class inventory."""
    if not isinstance(document, dict):
        raise ValueError("moduleinfo root must be a JSON object")

    name = document.get("Name")
    version = document.get("Version")
    factory = document.get("Factory Info")
    if not isinstance(name, str) or not name.strip():
        raise ValueError("moduleinfo Name is missing or empty")
    if expected_name is not None and name != expected_name:
        raise ValueError(f"moduleinfo Name mismatch: expected {expected_name!r}, got {name!r}")
    if not isinstance(version, str) or not _VERSION_RE.fullmatch(version):
        raise ValueError(f"moduleinfo Version is missing or invalid: {version!r}")
    if not isinstance(factory, dict):
        raise ValueError('moduleinfo "Factory Info" must be an object')

    vendor = factory.get("Vendor")
    if not isinstance(vendor, str) or not vendor.strip():
        raise ValueError('moduleinfo "Factory Info" Vendor is missing or empty')
    if expected_vendor is not None and vendor != expected_vendor:
        raise ValueError(
            f"moduleinfo Vendor mismatch: expected {expected_vendor!r}, got {vendor!r}"
        )

    classes = document.get("Classes")
    if not isinstance(classes, list) or not classes:
        raise ValueError("moduleinfo Classes must be a non-empty array")
    if len(classes) != _EXPECTED_CLASS_COUNT:
        raise ValueError(
            f"moduleinfo must contain exactly {_EXPECTED_CLASS_COUNT} VST3 classes, got {len(classes)}"
        )

    class_ids: set[str] = set()
    categories: set[str] = set()
    for index, entry in enumerate(classes):
        if not isinstance(entry, dict):
            raise ValueError(f"moduleinfo Classes[{index}] must be an object")
        cid = entry.get("CID")
        category = entry.get("Category")
        if not isinstance(cid, str) or not _CID_RE.fullmatch(cid):
            raise ValueError(
                f"moduleinfo Classes[{index}] CID must be exactly 32 hexadecimal characters"
            )
        if cid in class_ids:
            raise ValueError(f"duplicate VST3 class CID: {cid}")
        class_ids.add(cid)
        if not isinstance(category, str) or not category.strip():
            raise ValueError(f"moduleinfo Classes[{index}] has no Category")
        categories.add(category)
        if entry.get("Name") != name:
            raise ValueError(f"moduleinfo Classes[{index}] Name does not match module Name")
        if entry.get("Vendor") != vendor:
            raise ValueError(f"moduleinfo Classes[{index}] Vendor does not match factory Vendor")
        if entry.get("Version") != version:
            raise ValueError(f"moduleinfo Classes[{index}] Version does not match module Version")

    missing = _REQUIRED_CATEGORIES - categories
    if missing:
        raise ValueError("moduleinfo missing required VST3 classes: " + ", ".join(sorted(missing)))

    return {
        "name": name,
        "version": version,
        "vendor": vendor,
        "class_count": len(classes),
        "class_ids": sorted(class_ids),
    }


def normalize_text(
    source: str,
    expected_name: str | None = None,
    expected_vendor: str | None = None,
) -> tuple[str, dict[str, Any]]:
    """Normalize trailing commas, parse strict JSON, validate, and emit stable JSON."""
    candidate = strip_trailing_commas(source)
    document = json.loads(candidate)
    summary = validate_moduleinfo(document, expected_name, expected_vendor)
    normalized = json.dumps(document, ensure_ascii=False, indent=2) + "\n"
    # Assert the emitted representation is strict JSON before it can be staged.
    json.loads(normalized)
    return normalized, summary


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--file", required=True, type=Path, help="VST3 Contents/Resources/moduleinfo.json")
    parser.add_argument("--expected-name", required=True)
    parser.add_argument("--expected-vendor", required=True)
    args = parser.parse_args()

    try:
        source = args.file.read_text(encoding="utf-8-sig")
        normalized, summary = normalize_text(source, args.expected_name, args.expected_vendor)
        args.file.write_text(normalized, encoding="utf-8", newline="\n")
    except (OSError, UnicodeError, json.JSONDecodeError, ValueError) as error:
        parser.exit(1, f"moduleinfo validation failed: {error}\n")

    print(
        "Strict VST3 moduleinfo verified: "
        f"{summary['name']} {summary['version']} / {summary['vendor']} / "
        f"{summary['class_count']} unique classes"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
