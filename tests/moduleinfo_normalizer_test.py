#!/usr/bin/env python3
"""Regression tests for strict VST3 moduleinfo normalization."""
from __future__ import annotations

import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.normalize_vst3_moduleinfo import normalize_text, strip_trailing_commas  # noqa: E402


FIXTURE_WITH_TRAILING_COMMAS = r'''{
  "Name": "Skell Spacer",
  "Description": "literal ,} and ,] text must remain untouched",
  "Version": "1.2.0",
  "Factory Info": {
    "Vendor": "Skell Audio",
    "URL": "",
    "E-Mail": "",
    "Flags": {
      "Unicode": true,
      "Classes Discardable": false,
      "Component Non Discardable": false,
    },
  },
  "Classes": [
    {
      "CID": "ABCDEF019182FAEB536B4175536B3032",
      "Category": "Audio Module Class",
      "Name": "Skell Spacer",
      "Vendor": "Skell Audio",
      "Version": "1.2.0",
      "Sub Categories": ["Fx",],
      "Snapshots": [],
    },
    {
      "CID": "ABCDEF011234ABCD536B4175536B3032",
      "Category": "Component Controller Class",
      "Name": "Skell Spacer",
      "Vendor": "Skell Audio",
      "Version": "1.2.0",
      "Sub Categories": ["Fx",],
      "Snapshots": [],
    },
  ],
}'''


class ModuleInfoNormalizerTests(unittest.TestCase):
    def test_removes_nested_trailing_commas_and_preserves_string_contents(self) -> None:
        normalized, summary = normalize_text(
            FIXTURE_WITH_TRAILING_COMMAS,
            expected_name="Skell Spacer",
            expected_vendor="Skell Audio",
        )
        parsed = json.loads(normalized)
        self.assertEqual(parsed["Description"], "literal ,} and ,] text must remain untouched")
        self.assertEqual(summary["class_count"], 2)
        self.assertEqual(len(set(summary["class_ids"])), 2)

    def test_normalization_is_idempotent(self) -> None:
        first, _ = normalize_text(FIXTURE_WITH_TRAILING_COMMAS, "Skell Spacer", "Skell Audio")
        second, _ = normalize_text(first, "Skell Spacer", "Skell Audio")
        self.assertEqual(first, second)

    def test_strict_json_is_left_semantically_unchanged(self) -> None:
        source = strip_trailing_commas(FIXTURE_WITH_TRAILING_COMMAS)
        normalized, _ = normalize_text(source, "Skell Spacer", "Skell Audio")
        self.assertEqual(json.loads(source), json.loads(normalized))

    def test_rejects_wrong_product_identity(self) -> None:
        with self.assertRaisesRegex(ValueError, "Name mismatch"):
            normalize_text(FIXTURE_WITH_TRAILING_COMMAS, "Another Plugin", "Skell Audio")

    def test_rejects_duplicate_class_ids(self) -> None:
        duplicate = FIXTURE_WITH_TRAILING_COMMAS.replace(
            "ABCDEF011234ABCD536B4175536B3032",
            "ABCDEF019182FAEB536B4175536B3032",
        )
        with self.assertRaisesRegex(ValueError, "duplicate VST3 class CID"):
            normalize_text(duplicate, "Skell Spacer", "Skell Audio")

    def test_rejects_missing_controller_class(self) -> None:
        missing = FIXTURE_WITH_TRAILING_COMMAS.replace(
            '"Category": "Component Controller Class",',
            '"Category": "Audio Module Class",',
        )
        with self.assertRaisesRegex(ValueError, "missing required VST3 classes"):
            normalize_text(missing, "Skell Spacer", "Skell Audio")

    def test_rejects_non_trailing_json_syntax_errors(self) -> None:
        with self.assertRaises(json.JSONDecodeError):
            normalize_text('{"Name": "Skell Spacer",,,}', "Skell Spacer", "Skell Audio")

    def test_rejects_unexpected_third_class(self) -> None:
        document = json.loads(strip_trailing_commas(FIXTURE_WITH_TRAILING_COMMAS))
        extra = dict(document["Classes"][1])
        extra["CID"] = "0123456789ABCDEF0123456789ABCDEF"
        extra["Category"] = "Unexpected Class"
        document["Classes"].append(extra)
        with self.assertRaisesRegex(ValueError, "exactly 2 VST3 classes"):
            normalize_text(json.dumps(document), "Skell Spacer", "Skell Audio")

    def test_rejects_non_32_character_or_non_hex_cids(self) -> None:
        for invalid_cid in ("A" * 31, "G" * 32):
            with self.subTest(invalid_cid=invalid_cid):
                document = json.loads(strip_trailing_commas(FIXTURE_WITH_TRAILING_COMMAS))
                document["Classes"][0]["CID"] = invalid_cid
                with self.assertRaisesRegex(ValueError, "32 hexadecimal characters"):
                    normalize_text(json.dumps(document), "Skell Spacer", "Skell Audio")


if __name__ == "__main__":
    unittest.main(verbosity=2)
