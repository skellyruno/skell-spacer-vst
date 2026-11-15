#!/usr/bin/env python3
"""Static guard against accidentally dropping published plugin IDs."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
processor = (root / "Source/PluginProcessor.cpp").read_text()
cmake = (root / "CMakeLists.txt").read_text()
ids = set(re.findall(r'juce::ParameterID\{"([^"]+)",\s*1\}', processor))
legacy = {"amount", "smooth", "link", "attack", "release", "lowcut",
          "highcut", "freeze", "power", "pan", "ms", "liveMode"}
missing = sorted(legacy - ids)
assert not missing, f"Existing automation IDs removed: {missing}"
assert 'PLUGIN_CODE Sk02' in cmake, "Plugin identity changed"
assert 'PLUGIN_MANUFACTURER_CODE SkAu' in cmake, "Manufacturer identity changed"
assert 'BUNDLE_ID "audio.skell.spacer"' in cmake, "Bundle ID changed"
print(f"[PASS] preserved {len(legacy)} legacy parameter IDs and VST3 identity")
